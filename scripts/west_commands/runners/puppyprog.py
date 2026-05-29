# SPDX-License-Identifier: Apache-2.0

import os
import platform
import shutil
import shlex

from pathlib import Path
from runners.core import BuildConfiguration, RunnerCaps, ZephyrBinaryRunner
from runners.puppyheader import PuppyHeaderMaker

class PuppyProgBinaryRunner(ZephyrBinaryRunner):
    """Runner front-end for Puppy."""

    def __init__(self, cfg,
                 tool_path: Path | None,
                 port: Path | None,
                 timeout: int | None,
                 retries: int | None,
                 baud: int | None):
        super().__init__(cfg)
        self._tool_path = tool_path or PuppyProgBinaryRunner._get_tool_path()
        self._port = port
        self._timeout = timeout
        self._retries = retries
        self._baud = baud

    @classmethod
    def name(cls):
        return "puppyprog"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, hide_load_files=True)

    @staticmethod
    def _get_tool_path() -> Path:
        tool_path = shutil.which("mcumgrctl")
        if tool_path is not None:
            return Path(tool_path)
        if platform.system() == "Windows":
            return (Path(os.environ["USERPROFILE"]) /
                    ".cargo" / "bin" / "mcumgrctl.exe")
        return (Path.home() / ".cargo" / "bin" / "mcumgrctl")

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument("--tool-path", type=Path, required=False,
                            help="Path to mcumgrctl flash tool")
        parser.add_argument("--port", type=Path, required=False,
                            help="Use the given serial port as backend")
        parser.add_argument("--timeout", type=int, required=False,
                            help="Communication timeout (in ms)")
        parser.add_argument("--retries", type=int, required=False,
                            help="Retry count")
        parser.add_argument("--baud", type=int, required=False,
                            default=115200, help="Serial port baud rate")

    @classmethod
    def do_create(cls, cfg, args):
        return PuppyProgBinaryRunner(cfg, args.tool_path,
                                     args.port, args.timeout,
                                     args.retries, args.baud)

    def do_run(self, command: str, **kwargs):
        build_conf = BuildConfiguration(self.cfg.build_dir)
        if command == "flash":
            if build_conf.getboolean("CONFIG_BOOTLOADER_MCUBOOT"):
                self._flash_sig_binary(**kwargs)
            else:
                self._flash_raw_binary(**kwargs)
        else:
            raise NotImplementedError(f"command {command} is not supported")

    def _flash_raw_binary(self, **kwargs):
        bin_file = self.cfg.bin_file

        if bin_file is None:
            raise RuntimeError("no binary file was specified")
        elif not os.path.isfile(bin_file):
            raise RuntimeError(f"file {bin_file} does not exist")

        out_file = Path(self.cfg.build_dir) / "flash_output.bin"
        print(f"Input file: {bin_file}")
        print(f"Output file: {out_file}")

        maker = PuppyHeaderMaker(Path(bin_file), None, None)
        maker.make_bootable(str(out_file))

    def _flash_sig_binary(self, **kwargs):
        self.require(str(self._tool_path))

        if self._port is None:
            raise RuntimeError("no serial port was specified as a backend")

        bin_file = self.cfg.bin_file

        if bin_file is None:
            raise RuntimeError("no binary file was specified")
        elif not os.path.isfile(bin_file):
            raise RuntimeError(f"file {bin_file} does not exist")

        cmd_and_args  = [shlex.quote(str(self._tool_path))]
        cmd_and_args += ["-s", shlex.quote(str(self._port))]

        if self._baud:
            cmd_and_args += ["-b", f"{self._baud}"]
        if self._timeout:
            cmd_and_args += ["-t", f"{self._timeout}"]
        if self._retries:
            cmd_and_args += ["--retries", f"{self._retries}"]

        cmd_and_args += ["image", "upload", shlex.quote(bin_file)]
        self.check_call(cmd_and_args)

