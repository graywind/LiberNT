#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production USBPORT_Wait at passive and raised IRQL."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--revision', help='Git revision of usbport.c to test')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[3]
name = 'drivers/usb/usbport/usbport.c'
source = (subprocess.check_output(['git', 'show', f'{args.revision}:{name}'], cwd=root, text=True)
          if args.revision else (root/name).read_text())
start = source.index('NTSTATUS\nNTAPI\nUSBPORT_Wait(')
end = source.index('\nVOID\nNTAPI\nUSBPORT_MiniportInterrupts(', start)
with tempfile.TemporaryDirectory(prefix='usbport-wait-') as directory:
    work = Path(directory)
    (work/'wait.h').write_text(source[start:end])
    executable = work/'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(work), str(here/'wait_host.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
