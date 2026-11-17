# Logitech G435 LIGHTSPEED (Jieli 4C4A:4155) - Deep USB/HID/Audio Diagnostics Probe (v3)
# 100% Somente Leitura (Nao envia comandos de escrita aleatorios; preserva audio, mic e LIGHTSPEED)

param(
    [int]$ListenSeconds = 25,
    [switch]$DumpAllLocalHidForDebug
)

$code = @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public class HidDeepProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct HIDD_ATTRIBUTES {
        public int Size;
        public ushort VendorID;
        public ushort ProductID;
        public ushort VersionNumber;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct HIDP_CAPS {
        public ushort Usage;
        public ushort UsagePage;
        public ushort InputReportByteLength;
        public ushort OutputReportByteLength;
        public ushort FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes;
        public ushort NumberInputButtonCaps;
        public ushort NumberInputValueCaps;
        public ushort NumberInputDataIndices;
        public ushort NumberOutputButtonCaps;
        public ushort NumberOutputValueCaps;
        public ushort NumberOutputDataIndices;
        public ushort NumberFeatureButtonCaps;
        public ushort NumberFeatureValueCaps;
        public ushort NumberFeatureDataIndices;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct HIDP_LINK_COLLECTION_NODE {
        public ushort LinkUsage;
        public ushort LinkUsagePage;
        public ushort Parent;
        public ushort NumberOfChildren;
        public ushort NextSibling;
        public ushort FirstChild;
        public uint Flags;
        public IntPtr UserContext;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct HIDP_BUTTON_CAPS {
        [FieldOffset(0)] public ushort UsagePage;
        [FieldOffset(2)] public byte ReportID;
        [FieldOffset(3)] public byte IsAlias;
        [FieldOffset(4)] public ushort BitField;
        [FieldOffset(6)] public ushort LinkCollection;
        [FieldOffset(8)] public ushort LinkUsage;
        [FieldOffset(10)] public ushort LinkUsagePage;
        [FieldOffset(12)] public byte IsRange;
        [FieldOffset(13)] public byte IsStringRange;
        [FieldOffset(14)] public byte IsDesignatorRange;
        [FieldOffset(15)] public byte IsAbsolute;
        [FieldOffset(16)] public ushort ReportCount;
        [FieldOffset(18)] public ushort Reserved2;
        [FieldOffset(56)] public ushort UsageMin;
        [FieldOffset(58)] public ushort UsageMax;
        [FieldOffset(60)] public ushort StringMin;
        [FieldOffset(62)] public ushort StringMax;
        [FieldOffset(64)] public ushort DesignatorMin;
        [FieldOffset(66)] public ushort DesignatorMax;
        [FieldOffset(68)] public ushort DataIndexMin;
        [FieldOffset(70)] public ushort DataIndexMax;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct HIDP_VALUE_CAPS {
        [FieldOffset(0)] public ushort UsagePage;
        [FieldOffset(2)] public byte ReportID;
        [FieldOffset(3)] public byte IsAlias;
        [FieldOffset(4)] public ushort BitField;
        [FieldOffset(6)] public ushort LinkCollection;
        [FieldOffset(8)] public ushort LinkUsage;
        [FieldOffset(10)] public ushort LinkUsagePage;
        [FieldOffset(12)] public byte IsRange;
        [FieldOffset(13)] public byte IsStringRange;
        [FieldOffset(14)] public byte IsDesignatorRange;
        [FieldOffset(15)] public byte IsAbsolute;
        [FieldOffset(16)] public byte HasNull;
        [FieldOffset(17)] public byte Reserved;
        [FieldOffset(18)] public ushort BitSize;
        [FieldOffset(20)] public ushort ReportCount;
        [FieldOffset(32)] public uint UnitsExp;
        [FieldOffset(36)] public uint Units;
        [FieldOffset(40)] public int LogicalMin;
        [FieldOffset(44)] public int LogicalMax;
        [FieldOffset(48)] public int PhysicalMin;
        [FieldOffset(52)] public int PhysicalMax;
        [FieldOffset(56)] public ushort UsageMin;
        [FieldOffset(58)] public ushort UsageMax;
        [FieldOffset(60)] public ushort StringMin;
        [FieldOffset(62)] public ushort StringMax;
        [FieldOffset(64)] public ushort DesignatorMin;
        [FieldOffset(66)] public ushort DesignatorMax;
        [FieldOffset(68)] public ushort DataIndexMin;
        [FieldOffset(70)] public ushort DataIndexMax;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct OVERLAPPED {
        public IntPtr Internal;
        public IntPtr InternalHigh;
        public int Offset;
        public int OffsetHigh;
        public IntPtr hEvent;
    }

    [DllImport("hid.dll", SetLastError = true)]
    public static extern void HidD_GetHidGuid(out Guid hidGuid);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetAttributes(SafeFileHandle hidDeviceObject, ref HIDD_ATTRIBUTES attributes);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetPreparsedData(SafeFileHandle hidDeviceObject, out IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_FreePreparsedData(IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern int HidP_GetCaps(IntPtr preparsedData, ref HIDP_CAPS capabilities);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern int HidP_GetLinkCollectionNodes([Out] HIDP_LINK_COLLECTION_NODE[] linkCollectionNodes, ref uint linkCollectionNodesLength, IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern int HidP_GetButtonCaps(int reportType, [Out] HIDP_BUTTON_CAPS[] buttonCaps, ref ushort buttonCapsLength, IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern int HidP_GetValueCaps(int reportType, [Out] HIDP_VALUE_CAPS[] valueCaps, ref ushort valueCapsLength, IntPtr preparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetFeature(SafeFileHandle hidDeviceObject, byte[] lpReportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetInputReport(SafeFileHandle hidDeviceObject, byte[] lpReportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetProductString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetManufacturerString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetSerialNumberString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetNumInputBuffers(SafeFileHandle hidDeviceObject, out int numberBuffers);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, int flags);

    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVICE_INTERFACE_DATA {
        public int cbSize;
        public Guid interfaceClassGuid;
        public int flags;
        public IntPtr reserved;
    }

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiEnumDeviceInterfaces(IntPtr deviceInfoSet, IntPtr deviceInfoData, ref Guid interfaceClassGuid, int memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr deviceInfoSet, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, IntPtr deviceInterfaceDetailData, int deviceInterfaceDetailDataSize, ref int requiredSize, IntPtr deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern SafeFileHandle CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadFile(SafeFileHandle hFile, byte[] lpBuffer, int nNumberOfBytesToRead, IntPtr lpNumberOfBytesRead, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetOverlappedResult(SafeFileHandle hFile, IntPtr lpOverlapped, out int lpNumberOfBytesTransferred, bool bWait);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CancelIo(SafeFileHandle hFile);

    public static int ReadWithTimeout(SafeFileHandle h, byte[] buf, int timeoutMs, out int win32Err) {
        win32Err = 0;
        IntPtr hEvent = CreateEvent(IntPtr.Zero, true, false, null);
        OVERLAPPED ov = new OVERLAPPED();
        ov.hEvent = hEvent;
        IntPtr pOv = Marshal.AllocHGlobal(Marshal.SizeOf(ov));
        Marshal.StructureToPtr(ov, pOv, false);

        int got = -1;
        bool read = ReadFile(h, buf, buf.Length, IntPtr.Zero, pOv);
        int err = Marshal.GetLastWin32Error();
        if (read || err == 997) {
            uint wait = WaitForSingleObject(hEvent, (uint)timeoutMs);
            if (wait == 0) {
                if (!GetOverlappedResult(h, pOv, out got, false)) {
                    win32Err = Marshal.GetLastWin32Error();
                }
            } else {
                CancelIo(h);
                GetOverlappedResult(h, pOv, out got, true);
            }
        } else {
            win32Err = err;
        }
        CloseHandle(hEvent);
        Marshal.FreeHGlobal(pOv);
        return got;
    }

    public static string DecodeConsumerUsage(ushort page, ushort usage) {
        if (page == 0x0C) {
            switch (usage) {
                case 0x01: return "Consumer Control";
                case 0xB0: return "Play";
                case 0xB1: return "Pause";
                case 0xB3: return "Fast Forward";
                case 0xB5: return "Scan Next Track";
                case 0xB6: return "Scan Previous Track";
                case 0xB7: return "Rewind";
                case 0xCA: return "Tracking Increment";
                case 0xCB: return "Tracking Decrement";
                case 0xCC: return "Stop/Eject";
                case 0xCD: return "Play/Pause";
                case 0xE0: return "Volume";
                case 0xE2: return "Mute";
                case 0xE9: return "Volume Increment";
                case 0xEA: return "Volume Decrement";
                case 0x150: return "Balance Right";
                case 0x151: return "Balance Left";
            }
        }
        return "Usage_0x" + usage.ToString("X4");
    }
}

// Minimal COM Interop for Windows Core Audio / WASAPI Jack & Endpoint inspection
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
public class MMDeviceEnumeratorCom {}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int dataFlow, uint dwStateMask, out IMMDeviceCollection ppDevices);
    int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice ppEndpoint);
    int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string pwstrId, out IMMDevice ppDevice);
    int RegisterEndpointNotificationCallback(IntPtr pClient);
    int UnregisterEndpointNotificationCallback(IntPtr pClient);
}

[Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceCollection {
    int GetCount(out uint pcDevices);
    int Item(uint nDevice, out IMMDevice ppDevice);
}

[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDevice {
    int Activate(ref Guid iid, uint dwClsCtx, IntPtr pActivationParams, [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
    int OpenPropertyStore(uint stgmAccess, out IPropertyStore ppProperties);
    int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
    int GetState(out uint pdwState);
}

[StructLayout(LayoutKind.Sequential)]
public struct PROPERTYKEY {
    public Guid fmtid;
    public uint pid;
}

[Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IPropertyStore {
    int GetCount(out uint cProps);
    int GetAt(uint iProp, out PROPERTYKEY pkey);
    int GetValue(ref PROPERTYKEY key, out PROPVARIANT pv);
}

[StructLayout(LayoutKind.Explicit)]
public struct PROPVARIANT {
    [FieldOffset(0)] public ushort vt;
    [FieldOffset(8)] public IntPtr ptrVal;
    [FieldOffset(8)] public int intVal;
    [FieldOffset(8)] public uint uintVal;
}

[Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAudioEndpointVolume {
    int RegisterControlChangeNotify(IntPtr pNotify);
    int UnregisterControlChangeNotify(IntPtr pNotify);
    int GetChannelCount(out uint pnChannelCount);
    int SetMasterVolumeLevel(float fLevelDB, ref Guid pguidEventContext);
    int SetMasterVolumeLevelScalar(float fLevel, ref Guid pguidEventContext);
    int GetMasterVolumeLevel(out float pfLevelDB);
    int GetMasterVolumeLevelScalar(out float pfLevel);
    int SetChannelVolumeLevel(uint nChannel, float fLevelDB, ref Guid pguidEventContext);
    int SetChannelVolumeLevelScalar(uint nChannel, float fLevel, ref Guid pguidEventContext);
    int GetChannelVolumeLevel(uint nChannel, out float pfLevelDB);
    int GetChannelVolumeLevelScalar(uint nChannel, out float pfLevel);
    int SetMute([MarshalAs(UnmanagedType.Bool)] bool bMute, ref Guid pguidEventContext);
    int GetMute([MarshalAs(UnmanagedType.Bool)] out bool pbMute);
}

[Guid("2A07407E-6497-4A18-9787-32F79BD0D98F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IDeviceTopology {
    int GetConnectorCount(out uint pCount);
    int GetConnector(uint nIndex, out IConnector ppConnector);
    int GetSubunitCount(out uint pCount);
    int GetSubunit(uint nIndex, [MarshalAs(UnmanagedType.IUnknown)] out object ppSubunit);
    int GetPartById(uint nId, out IPart ppPart);
    int GetDeviceId([MarshalAs(UnmanagedType.LPWStr)] out string ppwstrDeviceId);
    int GetSignalPath(IPart pIPartFrom, IPart pIPartTo, [MarshalAs(UnmanagedType.Bool)] bool bRejectMixedPaths, out IntPtr ppParts);
}

[Guid("9c2c4058-23f5-41de-877a-df3af236a09e"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IConnector {
    int GetType(out int pType);
    int GetDataFlow(out int pFlow);
    int ConnectTo(IConnector pConnectTo);
    int Disconnect();
    int IsConnected([MarshalAs(UnmanagedType.Bool)] out bool pbConnected);
    int GetConnectedTo(out IConnector ppConTo);
    int GetConnectorIdConnectedTo([MarshalAs(UnmanagedType.LPWStr)] out string ppwstrConnectorId);
    int GetDeviceIdConnectedTo([MarshalAs(UnmanagedType.LPWStr)] out string ppwstrDeviceId);
}

[Guid("AE2DE0E4-5BCA-4F2D-AA46-5D13F8FDB3A9"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IPart {
    int GetName([MarshalAs(UnmanagedType.LPWStr)] out string ppwstrName);
    int GetLocalId(out uint pnId);
    int GetGlobalId([MarshalAs(UnmanagedType.LPWStr)] out string ppwstrGlobalId);
    int GetPartType(out int pPartType);
    int GetSubType(out Guid pSubType);
    int GetControlInterfaceCount(out uint pCount);
    int GetControlInterface(uint nIndex, [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
    int EnumPartsIncoming(out IntPtr ppParts);
    int EnumPartsOutgoing(out IntPtr ppParts);
    int GetTopologyObject(out IDeviceTopology ppTopology);
    int Activate(uint dwClsContext, ref Guid refiid, [MarshalAs(UnmanagedType.IUnknown)] out object ppvObject);
}

[Guid("4509F757-2D46-4637-8E62-CE7DB944F57B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IKsJackDescription {
    int GetJackCount(out uint pcJacks);
    int GetJackDescription(uint nJack, out KSJACK_DESCRIPTION pDescription);
}

[StructLayout(LayoutKind.Sequential)]
public struct KSJACK_DESCRIPTION {
    public uint ChannelMapping;
    public uint Color;
    public int ConnectionType;
    public int GeoLocation;
    public int GenLocation;
    public int PortConnection;
    [MarshalAs(UnmanagedType.Bool)] public bool IsConnected;
}

public static class AudioInspector {
    [DllImport("ole32.dll")]
    public static extern int PropVariantClear(ref PROPVARIANT pvar);

    public static void InspectG435Audio(bool dumpAll) {
        try {
            var enumerator = (IMMDeviceEnumerator)(new MMDeviceEnumeratorCom());
            IMMDeviceCollection coll;
            // eAll = 2, DEVICE_STATEMASK_ALL = 0x0000000F
            enumerator.EnumAudioEndpoints(2, 0x0000000F, out coll);
            uint count = 0;
            coll.GetCount(out count);

            Guid iidVolume = typeof(IAudioEndpointVolume).GUID;
            Guid iidTopo = typeof(IDeviceTopology).GUID;
            Guid iidJack = typeof(IKsJackDescription).GUID;
            PROPERTYKEY pkeyFriendly = new PROPERTYKEY { fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), pid = 14 };
            PROPERTYKEY pkeyContainer = new PROPERTYKEY { fmtid = new Guid("8c7ed206-3f8a-4827-b3ab-ae9e1faef6c7"), pid = 2 };

            for (uint i = 0; i < count; i++) {
                IMMDevice dev;
                coll.Item(i, out dev);
                string devId;
                dev.GetId(out devId);
                uint state = 0;
                dev.GetState(out state);

                IPropertyStore store;
                dev.OpenPropertyStore(0, out store);
                string friendly = "";
                PROPVARIANT pv;
                if (store.GetValue(ref pkeyFriendly, out pv) == 0 && pv.vt == 31 && pv.ptrVal != IntPtr.Zero) {
                    friendly = Marshal.PtrToStringUni(pv.ptrVal);
                }
                PropVariantClear(ref pv);

                if (!dumpAll && (friendly == null || friendly.IndexOf("G435", StringComparison.OrdinalIgnoreCase) < 0)) {
                    continue;
                }

                Console.WriteLine("  [Audio Endpoint] " + friendly + " (State=" + state + ", ID=" + devId + ")");

                try {
                    object volObj;
                    dev.Activate(ref iidVolume, 1, IntPtr.Zero, out volObj);
                    var vol = volObj as IAudioEndpointVolume;
                    if (vol != null) {
                        float scalar = 0;
                        bool mute = false;
                        vol.GetMasterVolumeLevelScalar(out scalar);
                        vol.GetMute(out mute);
                        Console.WriteLine(string.Format("    Volume: {0:F0}%  Mute: {1}", scalar * 100.0f, mute));
                    }
                } catch {}

                try {
                    object topoObj;
                    dev.Activate(ref iidTopo, 1, IntPtr.Zero, out topoObj);
                    var topo = topoObj as IDeviceTopology;
                    if (topo != null) {
                        uint connCount = 0;
                        topo.GetConnectorCount(out connCount);
                        for (uint c = 0; c < connCount; c++) {
                            IConnector conn;
                            topo.GetConnector(c, out conn);
                            bool isConn = false;
                            conn.IsConnected(out isConn);
                            string otherDev = "";
                            try { conn.GetDeviceIdConnectedTo(out otherDev); } catch {}
                            if (!string.IsNullOrEmpty(otherDev)) {
                                IMMDevice hwDev;
                                if (enumerator.GetDevice(otherDev, out hwDev) == 0) {
                                    // Inspect DeviceTopology of the adapter itself
                                }
                            }
                            IConnector connTo = null;
                            try { conn.GetConnectedTo(out connTo); } catch {}
                            if (connTo != null) {
                                var part = connTo as IPart;
                                if (part != null) {
                                    IDeviceTopology adapterTopo;
                                    part.GetTopologyObject(out adapterTopo);
                                    uint subCount = 0;
                                    adapterTopo.GetSubunitCount(out subCount);
                                    Console.WriteLine("    Adapter Topology Subunits: " + subCount);
                                    for (uint s = 0; s < subCount; s++) {
                                        object subObj;
                                        adapterTopo.GetSubunit(s, out subObj);
                                        var sp = subObj as IPart;
                                        if (sp != null) {
                                            string sname; Guid stype; uint ctrls = 0;
                                            sp.GetName(out sname);
                                            sp.GetSubType(out stype);
                                            sp.GetControlInterfaceCount(out ctrls);
                                            Console.WriteLine("      Subunit[" + s + "]: " + sname + " (Controls=" + ctrls + ")");
                                        }
                                    }
                                    var jack = part as IPart;
                                    try {
                                        object jackObj;
                                        jack.Activate(1, ref iidJack, out jackObj);
                                        var jd = jackObj as IKsJackDescription;
                                        if (jd != null) {
                                            uint jc = 0;
                                            jd.GetJackCount(out jc);
                                            for (uint j = 0; j < jc; j++) {
                                                KSJACK_DESCRIPTION desc;
                                                jd.GetJackDescription(j, out desc);
                                                Console.WriteLine(string.Format("      Jack[{0}]: IsConnected={1}, ConnType={2}, GenLoc={3}, PortConn={4}",
                                                    j, desc.IsConnected, desc.ConnectionType, desc.GenLocation, desc.PortConnection));
                                            }
                                        }
                                    } catch {}
                                }
                            }
                        }
                    }
                } catch {}
                if (dumpAll) break; // Only sample 1 when debugging locally
            }
        } catch (Exception ex) {
            Console.WriteLine("  AudioInspector Error: " + ex.Message);
        }
    }

    public static void GetMuteAndVol(string nameFilter, out string summary) {
        summary = "";
        try {
            var enumerator = (IMMDeviceEnumerator)(new MMDeviceEnumeratorCom());
            IMMDeviceCollection coll;
            enumerator.EnumAudioEndpoints(2, 0x00000001, out coll); // ACTIVE only
            uint count = 0;
            coll.GetCount(out count);
            Guid iidVolume = typeof(IAudioEndpointVolume).GUID;
            PROPERTYKEY pkeyFriendly = new PROPERTYKEY { fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), pid = 14 };
            for (uint i = 0; i < count; i++) {
                IMMDevice dev;
                coll.Item(i, out dev);
                IPropertyStore store;
                dev.OpenPropertyStore(0, out store);
                string friendly = "";
                PROPVARIANT pv;
                if (store.GetValue(ref pkeyFriendly, out pv) == 0 && pv.vt == 31 && pv.ptrVal != IntPtr.Zero) {
                    friendly = Marshal.PtrToStringUni(pv.ptrVal);
                }
                PropVariantClear(ref pv);
                if (friendly != null && friendly.IndexOf(nameFilter, StringComparison.OrdinalIgnoreCase) >= 0) {
                    object volObj;
                    dev.Activate(ref iidVolume, 1, IntPtr.Zero, out volObj);
                    var vol = volObj as IAudioEndpointVolume;
                    if (vol != null) {
                        float scalar = 0; bool mute = false;
                        vol.GetMasterVolumeLevelScalar(out scalar);
                        vol.GetMute(out mute);
                        summary += string.Format("[{0}: {1:F0}% Mute={2}] ", friendly, scalar * 100.0f, mute);
                    }
                }
            }
        } catch {}
    }
}
"@

try {
    Add-Type -TypeDefinition $code -Language CSharp
} catch {
    Write-Host "Erro compilando C# embarcado: $($_.Exception.Message)" -ForegroundColor Red
    return
}

$DIGCF_PRESENT = 0x02
$DIGCF_DEVICEINTERFACE = 0x10
$FILE_SHARE_READ = 0x01
$FILE_SHARE_WRITE = 0x02
$OPEN_EXISTING = 3
$GENERIC_READ = 0x80000000L
$GENERIC_WRITE = 0x40000000L
$FILE_FLAG_OVERLAPPED = 0x40000000L
$Marshal = [System.Runtime.InteropServices.Marshal]

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "===   DIAGNOSTICO PROFUNDO LOGITECH G435 (JIELI 4C4A:4155) - v3    ===" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host ""

# ---- 1. ENUMERACAO PNP COMPLETA POR CONTAINER ID ----
Write-Host "[ETAPA 1] Nos PnP do Windows no mesmo Container do dongle 4C4A:4155:" -ForegroundColor Yellow
$usbParents = Get-PnpDevice | Where-Object { $_.InstanceId -like "*VID_4C4A*PID_4155*" }
$containerIds = @()
foreach ($p in $usbParents) {
    $cidProp = Get-PnpDeviceProperty -InstanceId $p.InstanceId -KeyName "DEVPKEY_Device_ContainerId" -ErrorAction SilentlyContinue
    if ($cidProp -and $cidProp.Data) {
        if ($containerIds -notcontains $cidProp.Data) { $containerIds += $cidProp.Data }
    }
}

if ($containerIds.Count -eq 0 -and -not $DumpAllLocalHidForDebug) {
    Write-Host "  Nenhum dispositivo USB VID_4C4A&PID_4155 encontrado conectado." -ForegroundColor Red
} else {
    foreach ($cid in $containerIds) {
        Write-Host "  ContainerId: $cid" -ForegroundColor Cyan
        $allInContainer = Get-PnpDevice | Where-Object {
            $c = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName "DEVPKEY_Device_ContainerId" -ErrorAction SilentlyContinue).Data
            $c -eq $cid
        }
        foreach ($node in $allInContainer) {
            Write-Host "  -> [$($node.Status)] [$($node.Class)] $($node.FriendlyName) ($($node.InstanceId))" -ForegroundColor Green
            $props = Get-PnpDeviceProperty -InstanceId $node.InstanceId -ErrorAction SilentlyContinue | Where-Object {
                $_.Data -ne $null -and $_.KeyName -notlike "*Date*" -and $_.KeyName -notlike "*FriendlyName*"
            }
            foreach ($pr in $props) {
                if ($pr.Data -is [byte[]]) {
                    $take = [Math]::Min($pr.Data.Length, 16)
                    $hex = ($pr.Data[0..($take - 1)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
                    $valStr = "byte[$($pr.Data.Length)]: $hex"
                } elseif ($pr.Data -is [Array]) {
                    $valStr = ($pr.Data -join ", ")
                } else {
                    $valStr = "$($pr.Data)"
                }
                Write-Host "       $($pr.KeyName) = $valStr" -ForegroundColor Gray
            }
        }
    }
}

# ---- 2. INSPECAO DE TOPOLOGIA DE AUDIO (WASAPI / UAC) ----
Write-Host ""
Write-Host "[ETAPA 2] Topologia de Audio (WASAPI / Jack / Subunits):" -ForegroundColor Yellow
[AudioInspector]::InspectG435Audio([bool]$DumpAllLocalHidForDebug)

# ---- 3. RECONSTITUICAO COMPLETA DO DESCRITOR HID (HidP_*) ----
Write-Host ""
Write-Host "[ETAPA 3] Analise Completa das Colecoes e Capacidades HID (HidP_*):" -ForegroundColor Yellow

[Guid]$hidGuid = [Guid]::Empty
[HidDeepProbe]::HidD_GetHidGuid([ref]$hidGuid)
$devInfo = [HidDeepProbe]::SetupDiGetClassDevs([ref]$hidGuid, [IntPtr]::Zero, [IntPtr]::Zero, ($DIGCF_PRESENT -bor $DIGCF_DEVICEINTERFACE))

$index = 0
$found = 0
$targetPaths = @()

while ($true) {
    $ifData = New-Object HidDeepProbe+SP_DEVICE_INTERFACE_DATA
    $ifData.cbSize = $Marshal::SizeOf($ifData)
    if (-not [HidDeepProbe]::SetupDiEnumDeviceInterfaces($devInfo, [IntPtr]::Zero, [ref]$hidGuid, $index, [ref]$ifData)) { break }
    $index++

    $reqSize = 0
    [HidDeepProbe]::SetupDiGetDeviceInterfaceDetail($devInfo, [ref]$ifData, [IntPtr]::Zero, 0, [ref]$reqSize, [IntPtr]::Zero) | Out-Null
    if ($reqSize -le 0) { continue }

    $detailBuf = $Marshal::AllocHGlobal($reqSize)
    $Marshal::WriteInt32($detailBuf, 8)
    $path = ""
    if ([HidDeepProbe]::SetupDiGetDeviceInterfaceDetail($devInfo, [ref]$ifData, $detailBuf, $reqSize, [ref]$reqSize, [IntPtr]::Zero)) {
        $path = $Marshal::PtrToStringAuto([IntPtr]::Add($detailBuf, 4))
    }
    $Marshal::FreeHGlobal($detailBuf)
    if ([string]::IsNullOrEmpty($path)) { continue }

    $isCandidate = ($path -like "*4c4a*4155*") -or ($path -like "*046d*0adf*") -or ($path -like "*046d*0acb*")
    if ($DumpAllLocalHidForDebug -and $found -eq 0) { $isCandidate = $true }
    if (-not $isCandidate) { continue }

    $found++
    $targetPaths += $path
    Write-Host "  Dispositivo HID #$found : $path" -ForegroundColor Green

    $h = [HidDeepProbe]::CreateFile($path, 0, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($h.IsInvalid) {
        Write-Host "    Falha CreateFile(0) Win32Err=$($Marshal::GetLastWin32Error())" -ForegroundColor Red
        continue
    }

    $attr = New-Object HidDeepProbe+HIDD_ATTRIBUTES
    $attr.Size = $Marshal::SizeOf($attr)
    if ([HidDeepProbe]::HidD_GetAttributes($h, [ref]$attr)) {
        Write-Host ("    VID=0x{0:X4} PID=0x{1:X4} Ver=0x{2:X4}" -f $attr.VendorID, $attr.ProductID, $attr.VersionNumber)
    }

    foreach ($strInfo in @(
        @{ Name="Fabricante"; Fn={ param($hh, $b) [HidDeepProbe]::HidD_GetManufacturerString($hh, $b, $b.Length) } },
        @{ Name="Produto";    Fn={ param($hh, $b) [HidDeepProbe]::HidD_GetProductString($hh, $b, $b.Length) } },
        @{ Name="Serial";     Fn={ param($hh, $b) [HidDeepProbe]::HidD_GetSerialNumberString($hh, $b, $b.Length) } }
    )) {
        $buf = New-Object byte[] 256
        if (& $strInfo.Fn $h $buf) {
            $s = [System.Text.Encoding]::Unicode.GetString($buf).Split([char]0)[0]
            Write-Host "    $($strInfo.Name): `"$s`""
        }
    }

    $numInBuf = 0
    if ([HidDeepProbe]::HidD_GetNumInputBuffers($h, [ref]$numInBuf)) {
        Write-Host "    HidD_GetNumInputBuffers: $numInBuf"
    }

    $pp = [IntPtr]::Zero
    if ([HidDeepProbe]::HidD_GetPreparsedData($h, [ref]$pp)) {
        $caps = New-Object HidDeepProbe+HIDP_CAPS
        $st = [HidDeepProbe]::HidP_GetCaps($pp, [ref]$caps)
        Write-Host ("    HidP_GetCaps Status=0x{0:X8} | UsagePage=0x{1:X4} Usage=0x{2:X4}" -f $st, $caps.UsagePage, $caps.Usage) -ForegroundColor Cyan
        Write-Host ("    ReportLengths: Input={0} Output={1} Feature={2}" -f $caps.InputReportByteLength, $caps.OutputReportByteLength, $caps.FeatureReportByteLength) -ForegroundColor Cyan
        Write-Host ("    Counts: LinkNodes={0} | In(Btn={1},Val={2},Idx={3}) | Out(Btn={4},Val={5},Idx={6}) | Feat(Btn={7},Val={8},Idx={9})" -f `
            $caps.NumberLinkCollectionNodes,
            $caps.NumberInputButtonCaps, $caps.NumberInputValueCaps, $caps.NumberInputDataIndices,
            $caps.NumberOutputButtonCaps, $caps.NumberOutputValueCaps, $caps.NumberOutputDataIndices,
            $caps.NumberFeatureButtonCaps, $caps.NumberFeatureValueCaps, $caps.NumberFeatureDataIndices)

        if ($caps.NumberLinkCollectionNodes -gt 0) {
            [uint32]$nNodes = $caps.NumberLinkCollectionNodes
            $nodes = New-Object HidDeepProbe+HIDP_LINK_COLLECTION_NODE[] $nNodes
            [HidDeepProbe]::HidP_GetLinkCollectionNodes($nodes, [ref]$nNodes, $pp) | Out-Null
            for ($ni = 0; $ni -lt $nNodes; $ni++) {
                $nd = $nodes[$ni]
                Write-Host ("      LinkNode[{0}]: Page=0x{1:X4} Usage=0x{2:X4} Type=0x{3:X2} Parent={4} Children={5}" -f `
                    $ni, $nd.LinkUsagePage, $nd.LinkUsage, ($nd.Flags -band 0xFF), $nd.Parent, $nd.NumberOfChildren)
            }
        }

        foreach ($rt in @(@{Id=0; Name="Input"; Count=$caps.NumberInputButtonCaps}, @{Id=1; Name="Output"; Count=$caps.NumberOutputButtonCaps}, @{Id=2; Name="Feature"; Count=$caps.NumberFeatureButtonCaps})) {
            if ($rt.Count -gt 0) {
                [uint16]$bcLen = $rt.Count
                $bc = New-Object HidDeepProbe+HIDP_BUTTON_CAPS[] $bcLen
                [HidDeepProbe]::HidP_GetButtonCaps($rt.Id, $bc, [ref]$bcLen, $pp) | Out-Null
                for ($bi = 0; $bi -lt $bcLen; $bi++) {
                    $b = $bc[$bi]
                    $uDesc = if ($b.IsRange -ne 0) {
                        "Range 0x{0:X4}..0x{1:X4}" -f $b.UsageMin, $b.UsageMax
                    } else {
                        "0x{0:X4} ({1})" -f $b.UsageMin, [HidDeepProbe]::DecodeConsumerUsage($b.UsagePage, $b.UsageMin)
                    }
                    Write-Host ("      {0} ButtonCap[{1}]: ReportID={2} Page=0x{3:X4} {4} BitField=0x{5:X4} ReportCount={6} Abs={7} DataIdx={8}..{9}" -f `
                        $rt.Name, $bi, $b.ReportID, $b.UsagePage, $uDesc, $b.BitField, $b.ReportCount, $b.IsAbsolute, $b.DataIndexMin, $b.DataIndexMax) -ForegroundColor White
                }
            }
        }

        foreach ($rt in @(@{Id=0; Name="Input"; Count=$caps.NumberInputValueCaps}, @{Id=1; Name="Output"; Count=$caps.NumberOutputValueCaps}, @{Id=2; Name="Feature"; Count=$caps.NumberFeatureValueCaps})) {
            if ($rt.Count -gt 0) {
                [uint16]$vcLen = $rt.Count
                $vc = New-Object HidDeepProbe+HIDP_VALUE_CAPS[] $vcLen
                [HidDeepProbe]::HidP_GetValueCaps($rt.Id, $vc, [ref]$vcLen, $pp) | Out-Null
                for ($vi = 0; $vi -lt $vcLen; $vi++) {
                    $v = $vc[$vi]
                    Write-Host ("      {0} ValueCap[{1}]: ReportID={2} Page=0x{3:X4} Usage=0x{4:X4}..0x{5:X4} BitSize={6} ReportCount={7} Log={8}..{9}" -f `
                        $rt.Name, $vi, $v.ReportID, $v.UsagePage, $v.UsageMin, $v.UsageMax, $v.BitSize, $v.ReportCount, $v.LogicalMin, $v.LogicalMax) -ForegroundColor White
                }
            }
        }
        [HidDeepProbe]::HidD_FreePreparsedData($pp) | Out-Null
    }

    $h.Close()

    # Testar Feature e InputReport com log exato de Win32Error (somente leitura)
    $hProbe = [HidDeepProbe]::CreateFile($path, $GENERIC_READ, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($hProbe.IsInvalid) {
        $hProbe = [HidDeepProbe]::CreateFile($path, 0, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    }
    if (-not $hProbe.IsInvalid) {
        Write-Host "    Testando HidD_GetFeature / HidD_GetInputReport (IDs 0..255):" -ForegroundColor Gray
        $anyFeat = $false
        $anyIn = $false
        for ($rid = 0; $rid -le 255; $rid++) {
            $fbuf = New-Object byte[] 65
            $fbuf[0] = [byte]$rid
            if ([HidDeepProbe]::HidD_GetFeature($hProbe, $fbuf, $fbuf.Length)) {
                $anyFeat = $true
                $hex = ($fbuf[0..15] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
                Write-Host "      GetFeature(ID=$rid) -> OK: $hex" -ForegroundColor Green
            } else {
                $e = $Marshal::GetLastWin32Error()
                if ($rid -le 2) { Write-Host "      GetFeature(ID=$rid) -> FAIL Win32Err=$e" -ForegroundColor DarkGray }
            }
            $ibuf = New-Object byte[] 65
            $ibuf[0] = [byte]$rid
            if ([HidDeepProbe]::HidD_GetInputReport($hProbe, $ibuf, $ibuf.Length)) {
                $anyIn = $true
                $hex = ($ibuf[0..15] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
                Write-Host "      GetInputReport(ID=$rid) -> OK: $hex" -ForegroundColor Green
            } else {
                $e = $Marshal::GetLastWin32Error()
                if ($rid -le 2) { Write-Host "      GetInputReport(ID=$rid) -> FAIL Win32Err=$e" -ForegroundColor DarkGray }
            }
        }
        if (-not $anyFeat -and -not $anyIn) {
            Write-Host "      (Nenhum Report ID 0..255 respondeu a GetFeature ou GetInputReport)" -ForegroundColor DarkGray
        }
        $hProbe.Close()
    }
}

[HidDeepProbe]::SetupDiDestroyDeviceInfoList($devInfo) | Out-Null

# ---- 4. CAPTURA CONTINUA DE EVENTOS NO ENDPOINT 0x82 + ESTADO DE AUDIO ----
if ($targetPaths.Count -gt 0 -and -not $DumpAllLocalHidForDebug) {
    $path = $targetPaths[0]
    Write-Host ""
    Write-Host "[ETAPA 4] Captura continua no Endpoint 0x82 por $ListenSeconds segundos:" -ForegroundColor Yellow
    Write-Host "  Durante os proximos $ListenSeconds segundos, realize na sequencia (com ~2-3s entre cada):" -ForegroundColor Magenta
    Write-Host "   1) Aperte Volume +" -ForegroundColor Magenta
    Write-Host "   2) Aperte Volume -" -ForegroundColor Magenta
    Write-Host "   3) De um clique curto no botao MUTE (nao segure 3s)" -ForegroundColor Magenta
    Write-Host "   4) Desligue o headset (segure Power), aguarde 3s, ligue novamente" -ForegroundColor Magenta
    Write-Host "   5) (Opcional) Conecte/desconecte o cabo USB-C de carregamento no fone" -ForegroundColor Magenta
    Write-Host ""

    $ovHandle = [HidDeepProbe]::CreateFile($path, $GENERIC_READ, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, $FILE_FLAG_OVERLAPPED, [IntPtr]::Zero)
    if ($ovHandle.IsInvalid) {
        Write-Host "  Falha ao abrir $path para leitura overlapped. Err=$($Marshal::GetLastWin32Error())" -ForegroundColor Red
    } else {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $lastAudio = ""
        [AudioInspector]::GetMuteAndVol("G435", [ref]$lastAudio)
        Write-Host ("  [0.00s] Estado Audio Inicial: {0}" -f $lastAudio) -ForegroundColor Cyan

        while ($sw.Elapsed.TotalSeconds -lt $ListenSeconds) {
            $inBuf = New-Object byte[] 64
            $err = 0
            $n = [HidDeepProbe]::ReadWithTimeout($ovHandle, $inBuf, 500, [ref]$err)
            $tStr = "{0:F2}s" -f $sw.Elapsed.TotalSeconds
            if ($n -gt 0) {
                $hex = ($inBuf[0..($n - 1)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
                Write-Host ("  [{0}] HID IN ({1} bytes): [ {2} ]" -f $tStr, $n, $hex) -ForegroundColor Green
            } elseif ($err -ne 0) {
                Write-Host ("  [{0}] HID Read Error={1}" -f $tStr, $err) -ForegroundColor Red
                Start-Sleep -Milliseconds 300
            }

            $curAudio = ""
            [AudioInspector]::GetMuteAndVol("G435", [ref]$curAudio)
            if ($curAudio -ne $lastAudio) {
                Write-Host ("  [{0}] AUDIO MUDOU: {1}" -f $tStr, $curAudio) -ForegroundColor Yellow
                $lastAudio = $curAudio
            }
        }
        $ovHandle.Close()
        Write-Host "  Captura finalizada." -ForegroundColor Cyan
    }
}
