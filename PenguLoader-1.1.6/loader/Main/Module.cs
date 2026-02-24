using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;

namespace PenguLoader.Main
{
    static class Module
    {
        private static string ModuleName => "core.dll";
        private static string LoaderDir => AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\', '/');
        private static string ModulePath => Path.Combine(LoaderDir, ModuleName);

        // Only proxy d3d9.dll
        private static readonly string[] ProxyNames = { "d3d9.dll" };
        private static string ProxyName => ProxyNames[0];
        private static string ProxyPath => Path.Combine(Config.LeaguePath, ProxyName);

        private static string RoseConfigPath
        {
            get
            {
                var localAppData = DesktopUser.GetLocalAppData();
                return Path.Combine(localAppData, "Rose", "config.ini");
            }
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern bool WritePrivateProfileString(string section, string key, string value, string filePath);

        public static bool IsFound
        {
            get
            {
                var found = File.Exists(ModulePath);
                Logger.Debug("Module", $"IsFound: {found} (path: {ModulePath})");
                return found;
            }
        }

        public static bool IsLoaded
        {
            get
            {
                try
                {
                    // Check if any proxy is in use
                    if (LCU.IsValidDir(Config.LeaguePath))
                    {
                        foreach (var proxyName in ProxyNames)
                        {
                            var proxyPath = Path.Combine(Config.LeaguePath, proxyName);
                            if (File.Exists(proxyPath) && Utils.IsFileInUse(proxyPath))
                            {
                                Logger.Debug("Module", $"IsLoaded: true ({proxyName} is in use)");
                                return true;
                            }
                        }
                    }

                    var moduleInUse = Utils.IsFileInUse(ModulePath);
                    Logger.Debug("Module", $"IsLoaded (module): {moduleInUse} (path: {ModulePath})");
                    return moduleInUse;
                }
                catch (Exception ex)
                {
                    Logger.Error("Module", "IsLoaded check failed", ex);
                    return false;
                }
            }
        }

        public static bool IsActivated
        {
            get
            {
                try
                {
                    Logger.Debug("Module", "Checking IsActivated...");

                    if (!LCU.IsValidDir(Config.LeaguePath))
                    {
                        Logger.Debug("Module", $"IsActivated: false (LeaguePath invalid: '{Config.LeaguePath}')");
                        return false;
                    }

                    // Check disabled flag in config.ini
                    if (IsDisabledInConfig())
                    {
                        Logger.Debug("Module", "IsActivated: false (disabled in config.ini)");
                        return false;
                    }

                    // Check if ANY proxy exists with correct size
                    var coreSize = new FileInfo(ModulePath).Length;
                    foreach (var proxyName in ProxyNames)
                    {
                        var proxyPath = Path.Combine(Config.LeaguePath, proxyName);
                        if (File.Exists(proxyPath))
                        {
                            var proxySize = new FileInfo(proxyPath).Length;
                            if (proxySize == coreSize)
                            {
                                Logger.Debug("Module", $"IsActivated: true (found {proxyName}, size matches: {coreSize} bytes)");
                                return true;
                            }
                            else
                            {
                                Logger.Debug("Module", $"IsActivated: {proxyName} exists but size mismatch (core={coreSize}, proxy={proxySize})");
                            }
                        }
                    }

                    Logger.Debug("Module", "IsActivated: false (no valid proxy found)");
                    return false;
                }
                catch (Exception ex)
                {
                    Logger.Error("Module", "IsActivated check failed", ex);
                    return false;
                }
            }
        }

        public static bool SetActive(bool active)
        {
            Logger.Info("Module", $"SetActive called: active={active}");
            Logger.Info("Module", $"  LoaderDir: {LoaderDir}");
            Logger.Info("Module", $"  ModulePath: {ModulePath}");
            Logger.Info("Module", $"  LeaguePath: {Config.LeaguePath}");
            Logger.Info("Module", $"  ProxyPath: {ProxyPath}");
            Logger.Info("Module", $"  RoseConfigPath: {RoseConfigPath}");

            Logger.LogFileInfo(ModulePath, "core.dll");
            Logger.LogFileInfo(ProxyPath, $"{ProxyName} (proxy)");
            Logger.LogDirectoryInfo(Config.LeaguePath, "LeaguePath");

            var currentlyActivated = IsActivated;
            Logger.Info("Module", $"  CurrentlyActivated: {currentlyActivated}");

            if (currentlyActivated == active)
            {
                Logger.Info("Module", $"Already in desired state (active={active})");

                // Already deactivated in config, but proxy DLLs may still exist
                if (!active)
                {
                    foreach (var proxyName in ProxyNames)
                    {
                        var proxyPath = Path.Combine(Config.LeaguePath, proxyName);
                        try
                        {
                            if (File.Exists(proxyPath))
                            {
                                Logger.Info("Module", $"Cleaning up leftover proxy: {proxyName}");
                                File.Delete(proxyPath);
                                Logger.Info("Module", $"  {proxyName}: Deleted");
                            }
                        }
                        catch (Exception ex)
                        {
                            Logger.Warn("Module", $"  {proxyName}: Failed to delete: {ex.Message}");
                        }
                    }
                }
                return true;
            }

            if (!LCU.IsValidDir(Config.LeaguePath))
            {
                var msg = "League path is not set or invalid. Please set a valid League of Legends path first.";
                Logger.Error("Module", msg);
                throw new InvalidOperationException(msg);
            }

            // Run diagnostics to identify potential issues
            RunDiagnostics(Config.LeaguePath);

            if (active)
            {
                Logger.Info("Module", "=== ACTIVATING ===");
                Logger.Info("Module", $"Will copy core.dll as: {string.Join(", ", ProxyNames)}");

                // Clear disabled flag and set loader path in config.ini
                Logger.Info("Module", "Writing config: disabled=0");
                WriteConfigValue("disabled", "0");

                Logger.Info("Module", $"Writing config: loaderpath={LoaderDir}");
                WriteConfigValue("loaderpath", LoaderDir);

                var sourceSize = new FileInfo(ModulePath).Length;
                Logger.Info("Module", $"Source core.dll size: {sourceSize} bytes");

                // Copy core.dll as ALL proxy names
                foreach (var proxyName in ProxyNames)
                {
                    var proxyPath = Path.Combine(Config.LeaguePath, proxyName);

                    // Clear read-only attribute if present
                    ClearReadOnlyAttribute(proxyPath);

                    try
                    {
                        Logger.Info("Module", $"Copying core.dll -> {proxyName}...");

                        File.Copy(ModulePath, proxyPath, true);

                        // Verify the copy
                        if (File.Exists(proxyPath))
                        {
                            var destSize = new FileInfo(proxyPath).Length;
                            if (destSize == sourceSize)
                            {
                                Logger.Info("Module", $"  {proxyName}: OK ({destSize} bytes)");
                            }
                            else
                            {
                                Logger.Error("Module", $"  {proxyName}: SIZE MISMATCH! Expected {sourceSize}, got {destSize}");
                            }
                        }
                        else
                        {
                            Logger.Error("Module", $"  {proxyName}: File.Copy succeeded but file does not exist!");
                        }
                    }
                    catch (IOException ioEx) when (File.Exists(proxyPath))
                    {
                        var existingSize = new FileInfo(proxyPath).Length;
                        if (existingSize == sourceSize)
                        {
                            Logger.Info("Module", $"  {proxyName}: Locked but correct size - OK");
                        }
                        else
                        {
                            Logger.Warn("Module", $"  {proxyName}: Locked with wrong size ({existingSize} bytes): {ioEx.Message}");
                        }
                    }
                    catch (UnauthorizedAccessException uaEx)
                    {
                        Logger.Error("Module", $"  {proxyName}: PERMISSION DENIED: {uaEx.Message}");
                    }
                    catch (Exception ex)
                    {
                        Logger.Error("Module", $"  {proxyName}: FAILED: {ex.Message}");
                    }
                }

                Logger.Info("Module", "All proxy copies attempted");
            }
            else
            {
                Logger.Info("Module", "=== DEACTIVATING ===");

                Logger.Info("Module", "Writing config: disabled=1");
                WriteConfigValue("disabled", "1");

                Logger.Info("Module", "Writing config: loaderpath=");
                WriteConfigValue("loaderpath", "");

                // Delete ALL proxy DLLs
                foreach (var proxyName in ProxyNames)
                {
                    var proxyPath = Path.Combine(Config.LeaguePath, proxyName);
                    try
                    {
                        if (File.Exists(proxyPath))
                        {
                            Logger.Info("Module", $"Deleting proxy: {proxyName}");
                            File.Delete(proxyPath);
                            Logger.Info("Module", $"  {proxyName}: Deleted");
                        }
                    }
                    catch (Exception ex)
                    {
                        Logger.Warn("Module", $"  {proxyName}: Failed to delete (may be in use): {ex.Message}");
                    }
                }
            }

            var finalState = IsActivated;
            var success = finalState == active;
            Logger.Info("Module", $"SetActive completed: requested={active}, finalState={finalState}, success={success}");

            if (!success)
            {
                Logger.Error("Module", $"ACTIVATION MISMATCH! Requested {active} but ended up with {finalState}");
                Logger.LogFileInfo(ModulePath, "core.dll (final)");
                Logger.LogFileInfo(ProxyPath, $"{ProxyName} (final)");
                Logger.Info("Module", $"IsDisabledInConfig: {IsDisabledInConfig()}");
            }

            return success;
        }

        private static void WriteConfigValue(string key, string value)
        {
            var configPath = RoseConfigPath;
            Logger.Debug("Module", $"WriteConfigValue: [{key}]={value} to {configPath}");

            try
            {
                var dir = Path.GetDirectoryName(configPath);
                if (!Directory.Exists(dir))
                {
                    Logger.Info("Module", $"Creating config directory: {dir}");
                    Directory.CreateDirectory(dir);
                }

                var result = WritePrivateProfileString("General", key, value, configPath);
                Logger.Debug("Module", $"WritePrivateProfileString returned: {result}");

                if (!result)
                {
                    var error = Marshal.GetLastWin32Error();
                    Logger.Error("Module", $"WritePrivateProfileString failed with error code: {error}");
                }
            }
            catch (Exception ex)
            {
                Logger.Error("Module", $"WriteConfigValue failed for {key}", ex);
                throw;
            }
        }

        private static bool IsDisabledInConfig()
        {
            try
            {
                var configPath = RoseConfigPath;
                if (!File.Exists(configPath))
                {
                    Logger.Debug("Module", $"IsDisabledInConfig: config file does not exist ({configPath})");
                    return false;
                }

                var lines = File.ReadAllLines(configPath);
                bool inGeneral = false;
                foreach (var line in lines)
                {
                    var trimmed = line.Trim();
                    if (trimmed.StartsWith("[") && trimmed.EndsWith("]"))
                    {
                        inGeneral = trimmed.Equals("[General]", StringComparison.OrdinalIgnoreCase);
                        continue;
                    }
                    if (inGeneral)
                    {
                        var parts = trimmed.Split(new[] { '=' }, 2);
                        if (parts.Length == 2 && parts[0].Trim().Equals("disabled", StringComparison.OrdinalIgnoreCase))
                        {
                            var disabled = parts[1].Trim() == "1";
                            Logger.Debug("Module", $"IsDisabledInConfig: found disabled={parts[1].Trim()}, returning {disabled}");
                            return disabled;
                        }
                    }
                }

                Logger.Debug("Module", "IsDisabledInConfig: disabled key not found, returning false");
            }
            catch (Exception ex)
            {
                Logger.Error("Module", "IsDisabledInConfig failed", ex);
            }
            return false;
        }

        /// <summary>
        /// Run comprehensive diagnostics to identify permission/environment issues
        /// </summary>
        private static void RunDiagnostics(string leaguePath)
        {
            Logger.Info("Module", "=== RUNNING DIAGNOSTICS ===");

            // Check if running as admin
            try
            {
                var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                var isAdmin = principal.IsInRole(WindowsBuiltInRole.Administrator);
                Logger.Info("Module", $"Running as Administrator: {isAdmin}");
                Logger.Info("Module", $"Current user: {identity.Name}");

                // Check for desktop user mismatch
                if (DesktopUser.HasUserMismatch)
                {
                    Logger.Info("Module", $"Desktop user mismatch: Using '{DesktopUser.DesktopUsername}' data directory");
                    Logger.Info("Module", $"Desktop user LocalAppData: {DesktopUser.GetLocalAppData()}");
                }
            }
            catch (Exception ex)
            {
                Logger.Warn("Module", $"Failed to check admin status: {ex.Message}");
            }

            // Check League directory attributes
            try
            {
                var dirInfo = new DirectoryInfo(leaguePath);
                var attrs = dirInfo.Attributes;
                Logger.Info("Module", $"League directory attributes: {attrs}");

                if ((attrs & FileAttributes.ReadOnly) != 0)
                    Logger.Warn("Module", "WARNING: League directory has ReadOnly attribute!");
                if ((attrs & FileAttributes.System) != 0)
                    Logger.Warn("Module", "WARNING: League directory has System attribute!");
                if ((attrs & FileAttributes.Hidden) != 0)
                    Logger.Info("Module", "Note: League directory is Hidden");
            }
            catch (Exception ex)
            {
                Logger.Error("Module", $"Failed to get directory attributes: {ex.Message}");
            }

            // Check if proxy already exists and its attributes
            try
            {
                if (File.Exists(ProxyPath))
                {
                    var fileInfo = new FileInfo(ProxyPath);
                    Logger.Info("Module", $"Existing proxy file attributes: {fileInfo.Attributes}");
                    Logger.Info("Module", $"Existing proxy file size: {fileInfo.Length} bytes");
                    Logger.Info("Module", $"Existing proxy last modified: {fileInfo.LastWriteTime}");

                    if ((fileInfo.Attributes & FileAttributes.ReadOnly) != 0)
                        Logger.Warn("Module", "WARNING: Proxy file has ReadOnly attribute - will try to remove it");
                }
                else
                {
                    Logger.Info("Module", "Proxy file does not exist yet");
                }
            }
            catch (Exception ex)
            {
                Logger.Error("Module", $"Failed to check proxy file: {ex.Message}");
            }

            // Try to write a test file to verify we have write access
            var testFilePath = Path.Combine(leaguePath, ".rose_write_test");
            try
            {
                File.WriteAllText(testFilePath, "test");
                File.Delete(testFilePath);
                Logger.Info("Module", "Write test: SUCCESS - can write to League directory");
            }
            catch (UnauthorizedAccessException)
            {
                Logger.Error("Module", "Write test: FAILED - UnauthorizedAccessException - NO WRITE PERMISSION!");
            }
            catch (IOException ioEx)
            {
                Logger.Error("Module", $"Write test: FAILED - IOException: {ioEx.Message}");
            }
            catch (Exception ex)
            {
                Logger.Error("Module", $"Write test: FAILED - {ex.GetType().Name}: {ex.Message}");
            }

            // Check directory ACL/permissions
            try
            {
                var dirInfo = new DirectoryInfo(leaguePath);
                var acl = dirInfo.GetAccessControl();
                var rules = acl.GetAccessRules(true, true, typeof(NTAccount));

                Logger.Info("Module", $"Directory has {rules.Count} access rules:");
                foreach (FileSystemAccessRule rule in rules)
                {
                    if (rule.FileSystemRights.HasFlag(FileSystemRights.Write) ||
                        rule.FileSystemRights.HasFlag(FileSystemRights.FullControl))
                    {
                        Logger.Info("Module", $"  {rule.IdentityReference}: {rule.FileSystemRights} ({rule.AccessControlType})");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Warn("Module", $"Failed to read directory ACL: {ex.Message}");
            }

            // Check if League path is in Program Files (may need admin)
            var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            var programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            if (leaguePath.StartsWith(programFiles, StringComparison.OrdinalIgnoreCase) ||
                leaguePath.StartsWith(programFilesX86, StringComparison.OrdinalIgnoreCase))
            {
                Logger.Warn("Module", "WARNING: League is installed in Program Files - may require admin privileges!");
            }

            // Check for common antivirus folders that might interfere
            var avIndicators = new[]
            {
                @"C:\Program Files\Windows Defender",
                @"C:\Program Files\Malwarebytes",
                @"C:\Program Files\Norton",
                @"C:\Program Files\McAfee",
                @"C:\Program Files\Kaspersky Lab",
                @"C:\Program Files\Avast Software",
                @"C:\Program Files\AVG",
                @"C:\Program Files\ESET",
                @"C:\Program Files\Bitdefender"
            };

            foreach (var av in avIndicators)
            {
                if (Directory.Exists(av))
                {
                    Logger.Info("Module", $"Detected security software: {Path.GetFileName(av)}");
                }
            }

            // Log environment info
            Logger.Info("Module", $"OS: {Environment.OSVersion}");
            Logger.Info("Module", $"64-bit OS: {Environment.Is64BitOperatingSystem}");
            Logger.Info("Module", $"64-bit process: {Environment.Is64BitProcess}");

            // Log core.log location for troubleshooting
            var localAppData = DesktopUser.GetLocalAppData();
            var coreLogPath = Path.Combine(localAppData, "Rose", "core.log");
            Logger.Info("Module", $"Core.dll log location: {coreLogPath}");

            if (File.Exists(coreLogPath))
            {
                var coreLogInfo = new FileInfo(coreLogPath);
                Logger.Info("Module", $"  core.log exists, size: {coreLogInfo.Length} bytes, modified: {coreLogInfo.LastWriteTime}");
            }
            else
            {
                Logger.Warn("Module", "  core.log does NOT exist - DLL may not have been loaded by League");
                Logger.Warn("Module", "  If core.log doesn't appear after League starts, check:");
                Logger.Warn("Module", "    1. Antivirus may be blocking/quarantining the DLL");
                Logger.Warn("Module", "    2. Windows Defender SmartScreen may be blocking");
                Logger.Warn("Module", "    3. The DLL might not be compatible with this League version");
                Logger.Warn("Module", $"    4. League may not be loading {ProxyName} on this system");
            }

            // Check Windows Defender exclusions hint
            Logger.Info("Module", "TIP: If DLL is being blocked, add these exclusions to Windows Defender:");
            Logger.Info("Module", $"  - Folder: {LoaderDir}");
            Logger.Info("Module", $"  - Folder: {leaguePath}");

            Logger.Info("Module", "=== DIAGNOSTICS COMPLETE ===");
        }

        /// <summary>
        /// Remove read-only attribute from file if present
        /// </summary>
        private static void ClearReadOnlyAttribute(string filePath)
        {
            try
            {
                if (File.Exists(filePath))
                {
                    var attrs = File.GetAttributes(filePath);
                    if ((attrs & FileAttributes.ReadOnly) != 0)
                    {
                        Logger.Info("Module", $"Removing ReadOnly attribute from: {filePath}");
                        File.SetAttributes(filePath, attrs & ~FileAttributes.ReadOnly);
                        Logger.Info("Module", "ReadOnly attribute removed");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Warn("Module", $"Failed to clear ReadOnly attribute: {ex.Message}");
            }
        }
    }
}
