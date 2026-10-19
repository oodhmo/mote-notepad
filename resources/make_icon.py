# mote_icon.png → mote.ico 변환 스크립트 (아이콘을 바꿀 때만 실행: python resources/make_icon.py)
# - 문서 바깥의 흰 배경은 투명하게, 문서 안쪽은 흰색으로 남겨 어두운 작업 표시줄에서도 보이게 한다
# - 여백을 잘라 정사각형으로 맞춘 뒤 Windows가 쓰는 크기들을 한 파일에 담는다
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "mote_icon.png"
TARGET = HERE / "mote.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
MARGIN = 0.06  # 정사각형 한 변 대비 바깥 여백

image = Image.open(SOURCE).convert("RGBA")
gray = image.convert("L")

# 밝은 픽셀만 255로 둔 이진 이미지에서 모서리부터 채워 "바깥 영역"을 찾는다
mask = gray.point(lambda v: 255 if v > 128 else 0)
for corner in [(0, 0), (mask.width - 1, 0), (0, mask.height - 1), (mask.width - 1, mask.height - 1)]:
    if mask.getpixel(corner) == 255:
        ImageDraw.floodfill(mask, corner, 128)

alpha = mask.point(lambda v: 0 if v == 128 else 255)
image.putalpha(alpha)

# 내용 영역만 남기고 가운데 정렬된 정사각형 캔버스로 옮긴다
left, top, right, bottom = alpha.getbbox()
content = image.crop((left, top, right, bottom))
side = int(max(content.size) / (1 - MARGIN * 2))
canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
canvas.paste(content, ((side - content.width) // 2, (side - content.height) // 2))

canvas.save(TARGET, sizes=[(s, s) for s in SIZES])
print(f"saved {TARGET} ({', '.join(map(str, SIZES))})")
