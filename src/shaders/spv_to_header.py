"""Convert SPIR-V binaries to a C header with uint32_t arrays."""
import struct, sys, os

def spv_to_c_array(path, name):
    with open(path, 'rb') as f:
        data = f.read()
    words = struct.unpack(f'<{len(data) // 4}I', data)
    lines = [f'static const uint32_t {name}[] = {{']
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        lines.append('    ' + ', '.join(f'0x{w:08X}' for w in chunk) + ',')
    lines.append('};')
    return '\n'.join(lines)

shader_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__)

vert = spv_to_c_array(os.path.join(shader_dir, 'gothic_vert.spv'), 'g_gothic_vert_spv')
frag = spv_to_c_array(os.path.join(shader_dir, 'gothic_frag.spv'), 'g_gothic_frag_spv')
shadow_frag = spv_to_c_array(os.path.join(shader_dir, 'shadow_frag.spv'), 'g_shadow_frag_spv')

out = os.path.join(shader_dir, 'GothicShaders.h')
with open(out, 'w') as f:
    f.write('#pragma once\n#include <cstdint>\n\n' + vert + '\n\n' + frag + '\n\n' + shadow_frag + '\n')

print(f'  -> {out}')
