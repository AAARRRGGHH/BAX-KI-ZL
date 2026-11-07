from pathlib import Path
import sys
sys.path.insert(0,str(Path.cwd()/'.tools/launcher-python'))
from PIL import Image,ImageDraw,ImageFont
font=ImageFont.truetype('assets/fonts/Barlow-Regular.ttf',96)
box=font.getbbox('RESET');im=Image.new('L',(box[2]-box[0]+12,box[3]-box[1]+12));ImageDraw.Draw(im).text((6-box[0],6-box[1]),'RESET',font=font,fill=255)
runs=[]
for pixel in im.tobytes():
 if runs and runs[-1]>>8==pixel and runs[-1]&255<255:runs[-1]+=1
 else:runs.append((pixel<<8)|1)
Path('platform/desktop/reset_text_asset.hpp').write_text('#pragma once\n#include <cstdint>\nnamespace companion::text_art {\ninline constexpr int width='+str(im.width)+',height='+str(im.height)+';\ninline constexpr uint16_t mask[]={\n'+',\n'.join(','.join(hex(x) for x in runs[i:i+20]) for i in range(0,len(runs),20))+'\n};\n}\n')
