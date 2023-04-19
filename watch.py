#!/usr/bin/env python3

import os
import pathlib
import time
import subprocess
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

from figs import viewer

class PyExecHandler(FileSystemEventHandler):
    def __init__(self, dirpath):
        super(PyExecHandler, self).__init__()
        self.last_file = ""
        self.last_time = time.time()
        self.time_margin = 0.1
        self.dirpath = dirpath

    def on_moved(self, event):
        super(PyExecHandler, self).on_moved(event)
        if not event.is_directory:
            self._exec_script(event.dest_path)

    def on_created(self, event):
        super(PyExecHandler, self).on_created(event)
        if not event.is_directory:
            self._exec_script(event.src_path)

    def on_deleted(self, event):
        super(PyExecHandler, self).on_deleted(event)

    def on_modified(self, event):
        super(PyExecHandler, self).on_modified(event)
        if not event.is_directory:
            self._exec_script(event.src_path)

    def _exec_script(self, filepath):
        is_duplicated_write = self.last_file == filepath and time.time() < self.last_time + self.time_margin
        if pathlib.Path(filepath).suffix == ".py" and not is_duplicated_write:
            abspath = os.path.join(dirpath, filepath)
            print("\x1b[34m" + "Running '{}'...".format(filepath) + "\x1b[39m")
            try:
                subprocess.run(["python3", abspath], check=True)
            except subprocess.CalledProcessError:
                print("\x1b[31m" + "Execution of '{}' failed.".format(filepath) + "\x1b[39m")
            else:
                print("\x1b[34m" + "Done." + "\x1b[39m")
        self.last_file = filepath
        self.last_time = time.time()

if __name__ == "__main__":
    dirpath = pathlib.Path(__file__).parent
    os.chdir(dirpath)
    handler = PyExecHandler(dirpath)
    observer = Observer()
    observer.schedule(handler, "plot", recursive=True)
    observer.start()
    try:
        os.chdir("figs")
        viewer.launch_server()
    finally:
        observer.stop()
        observer.join()
