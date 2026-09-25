import asyncio
from qemu.qmp import QMPClient
import argparse
from subprocess import call
import os


def parse_cpus(spec: str) -> list[int]:
    """Parse '12-19' or '12,14,16-18' into a list of CPU ids."""
    cpus: list[int] = []
    for part in spec.split(','):
        part = part.strip()
        if '-' in part:
            lo, hi = part.split('-', 1)
            cpus.extend(range(int(lo), int(hi) + 1))
        elif part:
            cpus.append(int(part))
    if not cpus:
        raise argparse.ArgumentTypeError('empty cpu spec')
    return cpus


async def main():
    parser = argparse.ArgumentParser(description='Pin QEMU vCPUs to physical CPUs')
    parser.add_argument('-s', '--server', type=str, required=True, help='QMP server path or address:port')
    parser.add_argument('cpu', type=parse_cpus,
                        help='Physical CPUs: range (12-19), list (12,14,16) or mix')
    args = parser.parse_args()

    qmp = QMPClient('my-vm-nickname')
    await qmp.connect(args.server)

    devnull = open(os.devnull, 'w')

    res = await qmp.execute('query-cpus-fast')
    for vcpu in res:
        vcpuid = vcpu['cpu-index']
        tid = vcpu['thread-id']
        cpuid = args.cpu[vcpuid % len(args.cpu)]
        print(f"Pin vCPU {vcpuid} (tid {tid}) to physical CPU {cpuid}")
        try:
            call(['taskset', '-pc', str(cpuid), str(tid)], stdout=devnull)
        except OSError:
            print(f"Failed to pin vCPU{vcpuid} to CPU{cpuid}")

    await qmp.disconnect()

asyncio.run(main())
