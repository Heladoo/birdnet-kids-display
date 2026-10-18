"""Turn the host test's P4 .pbm files into 1x and 3x PNGs, black on white."""
import sys
from pathlib import Path

from PIL import Image

for p in Path(sys.argv[1]).glob("*.pbm"):
    im = Image.open(p).convert("L")  # PBM 1 = black; Pillow reads that correctly
    im.save(p.with_suffix(".png"))
    im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(p.with_name(p.stem + "@3x.png"))
