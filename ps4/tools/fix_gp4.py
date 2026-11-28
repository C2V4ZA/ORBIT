"""create-gp4 writes a fixed folder tree (sce_sys/about, sce_module, assets/audio ...). Replace the
<rootdir> block with the folders that the listed files really use. Usage: fix_gp4.py file.gp4"""
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
tree = {}
for path in re.findall(r'targ_path="([^"]+)"', s):
    node = tree
    for part in path.split('/')[:-1]:
        node = node.setdefault(part, {})
def emit(node, depth):
    out = ''
    for name in sorted(node):
        pad = '\t' * depth
        if node[name]:
            out += f'{pad}<dir targ_name="{name}">\n{emit(node[name], depth + 1)}{pad}</dir>\n'
        else:
            out += f'{pad}<dir targ_name="{name}" />\n'
    return out
s = re.sub(r'<rootdir>.*?</rootdir>', '<rootdir>\n' + emit(tree, 2) + '\t</rootdir>', s, flags=re.S)
open(p, 'w', encoding='utf-8').write(s)
