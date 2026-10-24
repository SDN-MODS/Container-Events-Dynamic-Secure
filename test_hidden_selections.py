import subprocess

out = subprocess.check_output(['grep', '-i', '-B', '10', '-A', '20', 'model="\\\\dz\\\\structures\\\\Industrial\\\\Containers', '/tmp/file_attachments/cpp DO DAYZ/config.cpp'])
print(out.decode('utf-8'))
