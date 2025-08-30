using System.Reflection;
using System.Runtime.InteropServices;

namespace UnrealSharp;

public class UnrealSharpDllImportResolver(IntPtr internalHandle)
{
    public IntPtr OnResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != "__Internal")
        {
            return IntPtr.Zero;
        }
        
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return Win32.GetModuleHandle(IntPtr.Zero);
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
        {
            return internalHandle;
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            return MacOS.dlopen(IntPtr.Zero, MacOS.RTLD_LAZY);
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Create("ANDROID")))
        {
            return Android.dlopen(IntPtr.Zero, Android.RTLD_LAZY);
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Create("IOS")))
        {
            return iOS.dlopen(IntPtr.Zero, iOS.RTLD_LAZY);
        }

        return IntPtr.Zero;
    }
    
    private static class MacOS
    {
        private const string SystemLibrary = "/usr/lib/libSystem.dylib";

        public const int RTLD_LAZY = 1;

        [DllImport(SystemLibrary)]
        public static extern IntPtr dlopen(IntPtr path, int mode);
    }

    private static class Win32
    {
        private const string SystemLibrary = "Kernel32.dll";

        [DllImport(SystemLibrary)]
        public static extern IntPtr GetModuleHandle(IntPtr lpModuleName);
    }

    private static class Android
    {
        private const string SystemLibrary = "libdl.so";

        public const int RTLD_LAZY = 1;

        [DllImport(SystemLibrary)]
        public static extern IntPtr dlopen(IntPtr filename, int flags);
    }

    private static class iOS
    {
        private const string SystemLibrary = "/usr/lib/libSystem.dylib";

        public const int RTLD_LAZY = 1;

        [DllImport(SystemLibrary)]
        public static extern IntPtr dlopen(IntPtr path, int mode);
    }
}