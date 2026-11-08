import subprocess
import sys


def run(*arguments):
    completed = subprocess.run([sys.argv[1], "run-apk", sys.argv[2], *arguments], check=True, capture_output=True, text=True)
    print(completed.stdout)
    return completed.stdout


output = run("--input", "0", "7", "--input", "1", "7", "--input", "2", "3", "--click", "Generate")
assert 'Landroid/widget/TextView; "7\n7\n7"' in output, "unchanged external APK did not calculate the requested sequence"
output = run("--input", "0", "10", "--input", "1", "1", "--click", "Generate")
assert "toast: Minimum is Greater than Maximum" in output, "external APK did not execute its validation branch"
print("external apk conformance passed")
