from shutil import copytree
import shutil
import zipfile
import json
import os
from pathlib import Path

with open('order.json','r') as f:
    order = json.loads(f.read())

mc_path = Path('mc')
try: 
    shutil.rmtree('mc')
except FileNotFoundError:
    pass
if not mc_path.exists():
    with zipfile.ZipFile(order['mc']['in'],'r') as f:
        f.extractall('mc')

mods_path = Path('mods')
if not mods_path.exists():
    mods_path.mkdir()

input_path = Path('input')
def patch(mod,in_jar):
    jar_path = input_path/mod
    with zipfile.ZipFile(jar_path,'r') as f:
        f.extractall('mod')
    mod_path = Path('mod')
    if not mod_path.exists():
        print(f"{mod_path} doesn't exist: {mod}")
    if not in_jar:
        for f in mod_path.iterdir():
            if f.name.startswith('mod_'):
                #then stick in mods folder and continue
                shutil.rmtree(mod_path)
                shutil.copy(jar_path,mods_path)
                return
    #patch
    shutil.copytree(mod_path,mc_path,dirs_exist_ok=True)
    shutil.rmtree(mod_path)
for mod in order['mods']:
    patch(mod,False)
for mod in order['in_jar']:
    patch(mod,True)
try: shutil.rmtree(mc_path / 'META-INF')
except FileNotFoundError: pass
out_archive_name = shutil.make_archive('output','zip',mc_path)
out_name = order['mc']['out']
out_path = Path(out_name)
out_jar = f"{out_name}.jar"
out_json = f"{out_name}.json"
try: 
    os.mkdir(out_path)
except FileExistsError: 
    pass
shutil.move(out_archive_name,Path(out_name)/out_jar)

import version

shutil.move(out_json,out_path/out_json)
out_zip = shutil.make_archive(out_name,'zip',out_name)
print(f"made {out_zip}")



