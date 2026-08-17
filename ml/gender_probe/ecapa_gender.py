"""ECAPA-TDNN binary voice-gender classifier, in an ONNX-exportable form.

Architecture and weights come from JaesungHuh/voice-gender-classifier (MIT), which
is itself a two-class head on TaoRuijie's ECAPA-TDNN, fine-tuned on the VoxCeleb2
dev set. The only substantive change here is the log-mel front end: upstream calls
`torchaudio.transforms.MelSpectrogram` inside `forward`, which builds a module on
every call and does not trace into a portable ONNX graph. This version precomputes
the analysis window, the DFT basis and the mel filterbank as buffers and runs the
STFT as a strided conv1d, so the whole thing, audio in and logits out, is a single
static graph made of Conv/Pad/MatMul/Log ops that onnxruntime-web can execute.

The reimplemented front end is verified numerically against torchaudio's
MelSpectrogram in `fetch_model.py`; the two agree to within float32 noise.

Output is a 2-vector of logits over {0: male, 1: female}, matching upstream's
`pred2gender`.
"""

from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F

# Front-end constants; these are upstream's MelSpectrogram arguments and must not
# drift, or the pretrained weights see a different input distribution.
SAMPLE_RATE = 16000
N_FFT = 512
WIN_LENGTH = 400
HOP_LENGTH = 160
F_MIN = 20.0
F_MAX = 7600.0
N_MELS = 80


def _hz_to_mel_htk(f: float) -> float:
    return 2595.0 * math.log10(1.0 + f / 700.0)


def _mel_to_hz_htk(m: torch.Tensor) -> torch.Tensor:
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def _mel_filterbank() -> torch.Tensor:
    """(n_freqs, n_mels) HTK-scale triangular filterbank, unnormalised.

    Matches torchaudio.functional.melscale_fbanks(norm=None, mel_scale="htk").
    """
    n_freqs = N_FFT // 2 + 1
    all_freqs = torch.linspace(0, SAMPLE_RATE // 2, n_freqs, dtype=torch.float64)

    m_min, m_max = _hz_to_mel_htk(F_MIN), _hz_to_mel_htk(F_MAX)
    m_pts = torch.linspace(m_min, m_max, N_MELS + 2, dtype=torch.float64)
    f_pts = _mel_to_hz_htk(m_pts)

    f_diff = f_pts[1:] - f_pts[:-1]                       # (n_mels + 1,)
    slopes = f_pts.unsqueeze(0) - all_freqs.unsqueeze(1)  # (n_freqs, n_mels + 2)
    down = -slopes[:, :-2] / f_diff[:-1]
    up = slopes[:, 2:] / f_diff[1:]
    fb = torch.clamp(torch.minimum(down, up), min=0.0)
    return fb.to(torch.float32)


def _dft_conv_kernel() -> torch.Tensor:
    """(2 * n_freqs, 1, n_fft) windowed DFT basis usable as a conv1d weight.

    Rows [0:n_freqs] give the real part, [n_freqs:] the imaginary part, so a
    strided conv1d over a centre-padded signal reproduces torch.stft exactly
    (up to the sign of the imaginary part, which the power spectrum discards).
    """
    n_freqs = N_FFT // 2 + 1
    window = torch.hamming_window(WIN_LENGTH, periodic=True, dtype=torch.float64)
    pad_left = (N_FFT - WIN_LENGTH) // 2
    win = torch.zeros(N_FFT, dtype=torch.float64)
    win[pad_left:pad_left + WIN_LENGTH] = window

    n = torch.arange(N_FFT, dtype=torch.float64)
    k = torch.arange(n_freqs, dtype=torch.float64).unsqueeze(1)
    angle = 2.0 * math.pi * k * n / N_FFT
    real = torch.cos(angle) * win
    imag = -torch.sin(angle) * win
    kernel = torch.cat([real, imag], dim=0).unsqueeze(1)
    return kernel.to(torch.float32)


class SEModule(nn.Module):
    def __init__(self, channels: int, bottleneck: int = 128) -> None:
        super().__init__()
        self.se = nn.Sequential(
            nn.AdaptiveAvgPool1d(1),
            nn.Conv1d(channels, bottleneck, kernel_size=1, padding=0),
            nn.ReLU(),
            nn.Conv1d(bottleneck, channels, kernel_size=1, padding=0),
            nn.Sigmoid(),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return x * self.se(x)


class Bottle2neck(nn.Module):
    def __init__(self, inplanes: int, planes: int, kernel_size: int,
                 dilation: int, scale: int = 8) -> None:
        super().__init__()
        width = int(math.floor(planes / scale))
        self.conv1 = nn.Conv1d(inplanes, width * scale, kernel_size=1)
        self.bn1 = nn.BatchNorm1d(width * scale)
        self.nums = scale - 1
        num_pad = math.floor(kernel_size / 2) * dilation
        self.convs = nn.ModuleList([
            nn.Conv1d(width, width, kernel_size=kernel_size,
                      dilation=dilation, padding=num_pad)
            for _ in range(self.nums)
        ])
        self.bns = nn.ModuleList([nn.BatchNorm1d(width) for _ in range(self.nums)])
        self.conv3 = nn.Conv1d(width * scale, planes, kernel_size=1)
        self.bn3 = nn.BatchNorm1d(planes)
        self.relu = nn.ReLU()
        self.width = width
        self.se = SEModule(planes)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        residual = x
        out = self.bn1(self.relu(self.conv1(x)))

        spx = torch.split(out, self.width, 1)
        sp = spx[0]
        for i in range(self.nums):
            if i > 0:
                sp = sp + spx[i]
            sp = self.bns[i](self.relu(self.convs[i](sp)))
            out = sp if i == 0 else torch.cat((out, sp), 1)
        out = torch.cat((out, spx[self.nums]), 1)

        out = self.bn3(self.relu(self.conv3(out)))
        out = self.se(out)
        return out + residual


class ECAPA_gender(nn.Module):
    """Upstream module, minus the huggingface mixin and torchaudio dependency."""

    pred2gender = {0: "male", 1: "female"}

    def __init__(self, C: int = 1024) -> None:
        super().__init__()
        self.C = C
        self.conv1 = nn.Conv1d(80, C, kernel_size=5, stride=1, padding=2)
        self.relu = nn.ReLU()
        self.bn1 = nn.BatchNorm1d(C)
        self.layer1 = Bottle2neck(C, C, kernel_size=3, dilation=2, scale=8)
        self.layer2 = Bottle2neck(C, C, kernel_size=3, dilation=3, scale=8)
        self.layer3 = Bottle2neck(C, C, kernel_size=3, dilation=4, scale=8)
        self.layer4 = nn.Conv1d(3 * C, 1536, kernel_size=1)
        self.attention = nn.Sequential(
            nn.Conv1d(4608, 256, kernel_size=1),
            nn.ReLU(),
            nn.BatchNorm1d(256),
            nn.Tanh(),
            nn.Conv1d(256, 1536, kernel_size=1),
            nn.Softmax(dim=2),
        )
        self.bn5 = nn.BatchNorm1d(3072)
        self.fc6 = nn.Linear(3072, 192)
        self.bn6 = nn.BatchNorm1d(192)
        self.fc7 = nn.Linear(192, 2)

        # Front-end tensors. Registered non-persistent so they neither collide
        # with nor are demanded by the upstream state_dict, but still travel into
        # the traced ONNX graph as initialisers.
        self.register_buffer("preemph", torch.tensor([[[-0.97, 1.0]]]), persistent=False)
        self.register_buffer("dft_kernel", _dft_conv_kernel(), persistent=False)
        self.register_buffer("mel_fb", _mel_filterbank(), persistent=False)

    def logtorchfbank(self, x: torch.Tensor) -> torch.Tensor:
        """(B, T) waveform in [-1, 1] at 16 kHz -> (B, 80, frames) log-mel."""
        x = x.unsqueeze(1)
        x = F.pad(x, (1, 0), mode="reflect")
        x = F.conv1d(x, self.preemph)                       # preemphasis

        x = F.pad(x, (N_FFT // 2, N_FFT // 2), mode="reflect")  # stft center=True
        spec = F.conv1d(x, self.dft_kernel, stride=HOP_LENGTH)
        n_freqs = N_FFT // 2 + 1
        power = spec[:, :n_freqs] ** 2 + spec[:, n_freqs:] ** 2

        mel = torch.matmul(self.mel_fb.transpose(0, 1), power) + 1e-6
        mel = mel.log()
        return mel - torch.mean(mel, dim=-1, keepdim=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.logtorchfbank(x)

        x = self.bn1(self.relu(self.conv1(x)))
        x1 = self.layer1(x)
        x2 = self.layer2(x + x1)
        x3 = self.layer3(x + x1 + x2)

        x = self.relu(self.layer4(torch.cat((x1, x2, x3), dim=1)))
        t = x.size()[-1]

        global_x = torch.cat((
            x,
            torch.mean(x, dim=2, keepdim=True).repeat(1, 1, t),
            torch.sqrt(torch.var(x, dim=2, keepdim=True).clamp(min=1e-4)).repeat(1, 1, t),
        ), dim=1)

        w = self.attention(global_x)
        mu = torch.sum(x * w, dim=2)
        sg = torch.sqrt((torch.sum((x ** 2) * w, dim=2) - mu ** 2).clamp(min=1e-4))

        x = torch.cat((mu, sg), 1)
        x = self.bn5(x)
        x = self.fc6(x)
        x = self.bn6(x)
        x = self.relu(x)
        return self.fc7(x)
