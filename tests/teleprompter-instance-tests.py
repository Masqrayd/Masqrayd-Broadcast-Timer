"""Exercise real named handles across processes on the Windows build runner."""
import os
import subprocess
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "teleprompter"))
from teleprompter_instance import InstanceGate

if len(sys.argv) > 1:
    gate = InstanceGate(sys.argv[1])
    primary = gate.acquire()
    gate.close()
    sys.exit(2 if primary else 0)

if os.name != "nt":
    print("Windows named-event test skipped on this platform")
    sys.exit(0)

name = "MasqraydTeleprompterTest_" + uuid.uuid4().hex
primary = InstanceGate(name)
assert primary.acquire()
child = subprocess.run([sys.executable, __file__, name], timeout=10)
assert child.returncode == 0, "Second process must signal existing reader and exit"
deadline = time.monotonic() + 3
while not primary.activation_requested():
    assert time.monotonic() < deadline, "Primary must receive activation request"
    time.sleep(0.01)
assert not primary.activation_requested(), "Activation event must auto-reset"
primary.close()
reopened = InstanceGate(name)
assert reopened.acquire(), "Closing the primary must permit relaunch"
reopened.close()
print("Windows single-instance and activation checks passed")
