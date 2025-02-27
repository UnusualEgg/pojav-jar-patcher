import json

with open('order.json','r') as f:
    out_name = json.loads(f.read())['mc']['out']
with open('b1.7.3.json','r') as f:
    v = json.loads(f.read())

del v['downloads']
v['mainClass'] = "net.minecraft.client.Minecraft"
v['id'] = out_name

with open(f'{out_name}.json','w') as f:
    f.write(json.dumps(v))
