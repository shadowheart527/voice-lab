"""Perceived-gender probe: a continuous 0..1 femininity read from windowed inference.

See README.md for the model, its licence, measured validation numbers and the
caveats that come with all of it.
"""

from typing import TYPE_CHECKING

__all__ = [
    "GenderProbe",
    "ProbeResult",
    "StreamingGenderProbe",
    "load_wav",
    "resample_to_16k",
]

if TYPE_CHECKING:  # pragma: no cover
    from .probe import (
        GenderProbe,
        ProbeResult,
        StreamingGenderProbe,
        load_wav,
        resample_to_16k,
    )


def __getattr__(name: str):
    """Import .probe lazily.

    Eagerly importing it here would pull in onnxruntime just to run
    `python -m gender_probe.fetch_model`, and would make `python -m
    gender_probe.probe` emit a runpy double-import warning on every call.
    """
    if name in __all__:
        from . import probe

        return getattr(probe, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
