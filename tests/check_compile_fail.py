import pathlib, subprocess, sys, tempfile
compiler, task, system = sys.argv[1:]
root = pathlib.Path(__file__).parent
for filename, diagnostic in [('throwing_move.cpp', 'nothrow move construction'), ('queue_on_worker.cpp', 'QueueDepth')]:
    with tempfile.TemporaryDirectory() as directory:
        result = subprocess.run([compiler, '-std=c++17', '-I'+task, '-I'+system, '-c', str(root/'compile_fail'/filename), '-o', str(pathlib.Path(directory)/'rejected.o')], capture_output=True, text=True)
        assert result.returncode != 0 and diagnostic in result.stderr, (filename, result.stderr)
print('Both invalid contracts rejected for the intended reason')
