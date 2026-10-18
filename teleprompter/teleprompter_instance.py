"""One Windows reader per login session, with a UI-thread activation request."""
import os


class InstanceGate:
    def __init__(self, name="MasqraydTeleprompter_v7_11"):
        self.name = name
        self.api = None
        self.mutex = None
        self.event = None
        self.closed = False

    def acquire(self):
        if os.name != "nt":
            return True
        import ctypes
        from ctypes import wintypes
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
        api.CreateMutexW.restype = wintypes.HANDLE
        api.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        api.CreateEventW.restype = wintypes.HANDLE
        api.SetEvent.argtypes = [wintypes.HANDLE]
        api.SetEvent.restype = wintypes.BOOL
        api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        api.WaitForSingleObject.restype = wintypes.DWORD
        api.CloseHandle.argtypes = [wintypes.HANDLE]
        api.CloseHandle.restype = wintypes.BOOL
        self.api = api
        ctypes.set_last_error(0)
        self.mutex = api.CreateMutexW(None, False, "Local\\" + self.name)
        error = ctypes.get_last_error()
        if not self.mutex:
            raise ctypes.WinError(error)
        existing = error == 183  # ERROR_ALREADY_EXISTS
        self.event = api.CreateEventW(None, False, False, "Local\\" + self.name + "_activate")
        if not self.event:
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)
        if existing:
            if not api.SetEvent(self.event):
                error = ctypes.get_last_error()
                self.close()
                raise ctypes.WinError(error)
            self.close()
            return False
        return True

    def activation_requested(self):
        return bool(self.event and not self.closed and self.api.WaitForSingleObject(self.event, 0) == 0)

    def bind(self, root, activate):
        if not self.event:
            return

        def poll():
            if self.closed:
                return
            if self.activation_requested():
                activate()
            root.after(150, poll)

        root.after(150, poll)

    def close(self):
        self.closed = True
        if self.api:
            for handle in (self.event, self.mutex):
                if handle:
                    self.api.CloseHandle(handle)
        self.event = self.mutex = None
