"""Compatibility shim so DeepFilterNet 0.5.6 imports under torchaudio >= 2.2.

DeepFilterNet 0.5.6 (the newest release on PyPI) does
``from torchaudio.backend.common import AudioMetaData`` at import time in
``df/io.py``. torchaudio removed the whole ``torchaudio.backend`` package, and
``torchaudio.info``, in 2.x. Nothing in the *inference* path actually needs
either -- it is only used by DeepFilterNet's own file loader, which we do not
use (we read wavs with soundfile and hand DeepFilterNet a tensor).

Import this module before importing ``df``.
"""

import sys
import types
from dataclasses import dataclass

import torchaudio


@dataclass
class AudioMetaData:
    sample_rate: int = 0
    num_frames: int = 0
    num_channels: int = 0
    bits_per_sample: int = 0
    encoding: str = "PCM_S"


def _install() -> None:
    if "torchaudio.backend.common" in sys.modules:
        return

    backend = types.ModuleType("torchaudio.backend")
    common = types.ModuleType("torchaudio.backend.common")
    common.AudioMetaData = AudioMetaData
    backend.common = common

    sys.modules["torchaudio.backend"] = backend
    sys.modules["torchaudio.backend.common"] = common
    torchaudio.backend = backend

    if not hasattr(torchaudio, "AudioMetaData"):
        torchaudio.AudioMetaData = AudioMetaData

    if not hasattr(torchaudio, "info"):
        import soundfile as sf

        def info(path, *_args, **_kwargs):
            i = sf.info(str(path))
            return AudioMetaData(
                sample_rate=i.samplerate,
                num_frames=i.frames,
                num_channels=i.channels,
            )

        torchaudio.info = info


_install()
