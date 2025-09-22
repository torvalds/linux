import lit.util
import os
import sys

main_config = lit.util.abs_path_preserve_drive(sys.argv[1])
main_config = os.path.normcase(main_config)

config_map = {main_config: sys.argv[2]}
builtin_parameters = {"config_map": config_map}

if __name__ == "__main__":
    from lit.main import main

    main_config_dir = os.path.dirname(main_config)
    sys.argv = [sys.argv[0]] + sys.argv[3:] + [main_config_dir]
    main(builtin_parameters)
