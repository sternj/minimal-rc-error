import sys
import force_rc_error
import os 
import subprocess

def get_preload_environ():
    env = {}
    # Prepend the Scalene library to the LD_PRELOAD list, if any
    new_ld_preload = os.path.join(
        force_rc_error.__path__[0].replace(" ", r"\ "), "libscalene.so"
    )
    if "LD_PRELOAD" in env:
        old_ld_preload = env["LD_PRELOAD"]
        env["LD_PRELOAD"] = new_ld_preload + ":" + old_ld_preload
    else:
        env["LD_PRELOAD"] = new_ld_preload
    # Disable command-line specified PYTHONMALLOC.
    if "PYTHONMALLOC" in env:
        del env["PYTHONMALLOC"]
    return env
if __name__ == '__main__':
    new_args = [
                sys.executable,
                "-m",
                "force_rc_error",
            ] + sys.argv[1:]
    req_env = get_preload_environ()
    if any(k_v not in os.environ.items() for k_v in req_env.items()):
        os.environ.update(req_env)
        new_args = [
            sys.executable,
            "-m",
            "force_rc_error",
        ] + sys.argv[1:]
        result = subprocess.Popen(new_args, close_fds=True, shell=False)
        try:
            result.wait()
        except KeyboardInterrupt:
            pass
        sys.exit(0)
    else:
        force_rc_error.main()