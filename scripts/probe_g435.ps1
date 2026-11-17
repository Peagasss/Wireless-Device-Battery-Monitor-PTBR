# Logitech G435 LIGHTSPEED (Receptor Jieli 4C4A:4155) - Script de Sondagem HID Somente Leitura
#
# Regras estritas de operacao:
#  - Somente leitura (nao envia Output Reports, SetFeature ou comandos de escrita).
#  - Identificacao estrita por VID/PID exato (0x4C4A:0x4155, 0x046D:0x0ADF, 0x046D:0x0ACB) via HidD_GetAttributes.
#  - Utiliza exclusivamente os tamanhos de relatorio declarados por HidP_GetCaps (InputReportByteLength / FeatureReportByteLength).
#  - Para o receptor Jieli 4C4A:4155 (relatorios nao numerados, InputReportByteLength = 3), testa apenas Report ID 0 com buffer exato de 3 bytes.
#  - Nao testa Report IDs de 1 a 255 indiscriminadamente.
#  - Nao executa HidD_GetFeature quando FeatureReportByteLength == 0.
#  - Separa os resultados retornados pela API Win32 HID de evidencias de barramento USB (nao assume STALL fisico a partir de GetLastError).

param(
    [int]$CaptureSeconds = 15
)

$code = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public class G435HidProbe {
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
    public struct OVERLAPPED {
        public IntPtr Internal;
        public IntPtr InternalHigh;
        public int Offset;
        public int OffsetHigh;
        public IntPtr hEvent;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SP_DEVICE_INTERFACE_DATA {
        public int cbSize;
        public Guid interfaceClassGuid;
        public int flags;
        public IntPtr reserved;
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
    public static extern bool HidD_GetInputReport(SafeFileHandle hidDeviceObject, byte[] lpReportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetFeature(SafeFileHandle hidDeviceObject, byte[] lpReportBuffer, int reportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetManufacturerString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetProductString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    public static extern bool HidD_GetSerialNumberString(SafeFileHandle hidDeviceObject, byte[] buffer, int bufferLength);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr hwndParent, int flags);

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

    [StructLayout(LayoutKind.Sequential)]
    public struct DEVPROPKEY {
        public Guid fmtid;
        public uint pid;
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    public static extern uint CM_Get_Device_ID_List_SizeW(out uint pulLen, string pszFilter, uint ulFlags);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    public static extern uint CM_Get_Device_ID_ListW(string pszFilter, char[] Buffer, uint BufferLen, uint ulFlags);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    public static extern uint CM_Locate_DevNodeW(out uint pdnDevInst, string pDeviceID, uint ulFlags);

    [DllImport("cfgmgr32.dll")]
    public static extern uint CM_Get_DevNode_PropertyW(uint dnDevInst, ref DEVPROPKEY PropertyKey, out uint PropertyType, byte[] PropertyBuffer, ref uint PropertyBufferSize, uint ulFlags);

    public static int ReadDevNodeByteProperty(string instanceId, Guid guid, uint pid) {
        uint devInst = 0;
        if (CM_Locate_DevNodeW(out devInst, instanceId, 0) != 0) return -1;
        DEVPROPKEY key = new DEVPROPKEY { fmtid = guid, pid = pid };
        byte[] buf = new byte[1];
        uint size = 1;
        uint type = 0;
        if (CM_Get_DevNode_PropertyW(devInst, ref key, out type, buf, ref size, 0) != 0) return -1;
        if (type == 0x03 && size >= 1) return buf[0]; // DEVPROP_TYPE_BYTE = 0x03
        if (type == 0x11 && size >= 1) return buf[0] != 0 ? 1 : 0; // DEVPROP_TYPE_BOOLEAN = 0x11
        return -1;
    }

    public static string ReadDevNodeStringProperty(string instanceId, Guid guid, uint pid) {
        uint devInst = 0;
        if (CM_Locate_DevNodeW(out devInst, instanceId, 0) != 0) return "";
        DEVPROPKEY key = new DEVPROPKEY { fmtid = guid, pid = pid };
        uint size = 0;
        uint type = 0;
        uint st = CM_Get_DevNode_PropertyW(devInst, ref key, out type, null, ref size, 0);
        if ((st != 0 && st != 0x1A) || size == 0 || type != 0x12) return ""; // CR_BUFFER_SMALL = 0x1A, DEVPROP_TYPE_STRING = 0x12
        byte[] buf = new byte[size];
        if (CM_Get_DevNode_PropertyW(devInst, ref key, out type, buf, ref size, 0) != 0) return "";
        return Encoding.Unicode.GetString(buf).TrimEnd('\0').Trim();
    }

    public static string[] EnumerateBthEnumInstances() {
        uint len = 0;
        if (CM_Get_Device_ID_List_SizeW(out len, "BTHENUM", 1) != 0 || len == 0) return new string[0];
        char[] buf = new char[len];
        if (CM_Get_Device_ID_ListW("BTHENUM", buf, len, 1) != 0) return new string[0];
        return new string(buf).Split(new char[] { '\0' }, StringSplitOptions.RemoveEmptyEntries);
    }

    public static int ReadInterruptReport(SafeFileHandle h, byte[] buf, int timeoutMs, out int win32Err) {
        win32Err = 0;
        IntPtr hEvent = CreateEvent(IntPtr.Zero, true, false, null);
        OVERLAPPED ov = new OVERLAPPED { hEvent = hEvent };
        IntPtr pOv = Marshal.AllocHGlobal(Marshal.SizeOf(ov));
        Marshal.StructureToPtr(ov, pOv, false);

        int got = -1;
        bool ok = ReadFile(h, buf, buf.Length, IntPtr.Zero, pOv);
        int err = Marshal.GetLastWin32Error();
        if (ok || err == 997) { // 997 = ERROR_IO_PENDING
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
}
"@

try {
    Add-Type -TypeDefinition $code -Language CSharp
} catch {
    Write-Host "Erro ao compilar definicoes P/Invoke: $($_.Exception.Message)" -ForegroundColor Red
    return
}

$DIGCF_PRESENT = 0x02
$DIGCF_DEVICEINTERFACE = 0x10
$FILE_SHARE_READ = 0x01
$FILE_SHARE_WRITE = 0x02
$OPEN_EXISTING = 3
$GENERIC_READ = 0x80000000L
$FILE_FLAG_OVERLAPPED = 0x40000000L
$HIDP_STATUS_SUCCESS = 0x00110000
$Marshal = [System.Runtime.InteropServices.Marshal]

function Get-HidString {
    param([Microsoft.Win32.SafeHandles.SafeFileHandle]$Handle, [scriptblock]$Getter)
    $buf = New-Object byte[] 256
    if (& $Getter $Handle $buf) {
        return [System.Text.Encoding]::Unicode.GetString($buf).Split([char]0)[0]
    }
    return ""
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "===  RELATORIO DE SONDAGEM HID - LOGITECH G435 (JIELI 4C4A:4155)   ===" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host ""

[Guid]$hidGuid = [Guid]::Empty
[G435HidProbe]::HidD_GetHidGuid([ref]$hidGuid)
$devInfo = [G435HidProbe]::SetupDiGetClassDevs([ref]$hidGuid, [IntPtr]::Zero, [IntPtr]::Zero, ($DIGCF_PRESENT -bor $DIGCF_DEVICEINTERFACE))

if ($devInfo -eq [IntPtr]::Zero -or $devInfo -eq -1) {
    Write-Host "Erro ao enumerar interfaces HID." -ForegroundColor Red
    return
}

$index = 0
$matchedDevices = 0

while ($true) {
    $ifData = New-Object G435HidProbe+SP_DEVICE_INTERFACE_DATA
    $ifData.cbSize = $Marshal::SizeOf($ifData)
    if (-not [G435HidProbe]::SetupDiEnumDeviceInterfaces($devInfo, [IntPtr]::Zero, [ref]$hidGuid, $index, [ref]$ifData)) {
        break
    }
    $index++

    $reqSize = 0
    [G435HidProbe]::SetupDiGetDeviceInterfaceDetail($devInfo, [ref]$ifData, [IntPtr]::Zero, 0, [ref]$reqSize, [IntPtr]::Zero) | Out-Null
    if ($reqSize -le 0) { continue }

    $detailBuf = $Marshal::AllocHGlobal($reqSize)
    $Marshal::WriteInt32($detailBuf, $(if ([IntPtr]::Size -eq 8) { 8 } else { 6 }))
    $path = ""
    if ([G435HidProbe]::SetupDiGetDeviceInterfaceDetail($devInfo, [ref]$ifData, $detailBuf, $reqSize, [ref]$reqSize, [IntPtr]::Zero)) {
        $path = $Marshal::PtrToStringAuto([IntPtr]::Add($detailBuf, 4))
    }
    $Marshal::FreeHGlobal($detailBuf)
    if ([string]::IsNullOrEmpty($path)) { continue }

    # Abrir em modo de consulta (dwDesiredAccess = 0) para validar VID/PID exato via HidD_GetAttributes
    $hQuery = [G435HidProbe]::CreateFile($path, 0, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($hQuery.IsInvalid) { continue }

    $attr = New-Object G435HidProbe+HIDD_ATTRIBUTES
    $attr.Size = $Marshal::SizeOf($attr)
    $hasAttr = [G435HidProbe]::HidD_GetAttributes($hQuery, [ref]$attr)

    if (-not $hasAttr) {
        $hQuery.Close()
        continue
    }

    # Filtragem estrita pelo VID/PID exato dos receptores G435 conhecidos
    $isJieliG435  = ($attr.VendorID -eq 0x4C4A -and $attr.ProductID -eq 0x4155)
    $isHoltekG435 = ($attr.VendorID -eq 0x046D -and $attr.ProductID -eq 0x0ADF)
    $isRealtekG435 = ($attr.VendorID -eq 0x046D -and $attr.ProductID -eq 0x0ACB)

    if (-not ($isJieliG435 -or $isHoltekG435 -or $isRealtekG435)) {
        $hQuery.Close()
        continue
    }

    $matchedDevices++
    $mfg  = Get-HidString -Handle $hQuery -Getter { param($hh, $b) [G435HidProbe]::HidD_GetManufacturerString($hh, $b, $b.Length) }
    $prod = Get-HidString -Handle $hQuery -Getter { param($hh, $b) [G435HidProbe]::HidD_GetProductString($hh, $b, $b.Length) }
    $ser  = Get-HidString -Handle $hQuery -Getter { param($hh, $b) [G435HidProbe]::HidD_GetSerialNumberString($hh, $b, $b.Length) }

    $miMatch = [regex]::Match($path, "(?i)&mi_([0-9a-f]{2})")
    $ifaceStr = if ($miMatch.Success) { "MI_$($miMatch.Groups[1].Value.ToUpper())" } else { "N/A" }

    Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
    Write-Host "[1] IDENTIFICACAO DO DISPOSITIVO E INTERFACE" -ForegroundColor Yellow
    Write-Host ("    Device Path : {0}" -f $path) -ForegroundColor White
    Write-Host ("    Interface   : {0}" -f $ifaceStr) -ForegroundColor White
    Write-Host ("    VID / PID   : 0x{0:X4} / 0x{1:X4} (Rev: 0x{2:X4})" -f $attr.VendorID, $attr.ProductID, $attr.VersionNumber) -ForegroundColor White
    Write-Host ("    Fabricante  : {0}" -f $mfg) -ForegroundColor White
    Write-Host ("    Produto     : {0}" -f $prod) -ForegroundColor White
    Write-Host ("    Serial      : {0}" -f $ser) -ForegroundColor White

    $pp = [IntPtr]::Zero
    if (-not [G435HidProbe]::HidD_GetPreparsedData($hQuery, [ref]$pp)) {
        Write-Host ("    Erro: HidD_GetPreparsedData falhou (Win32Error={0})." -f $Marshal::GetLastWin32Error()) -ForegroundColor Red
        $hQuery.Close()
        continue
    }

    $caps = New-Object G435HidProbe+HIDP_CAPS
    $capsStatus = [G435HidProbe]::HidP_GetCaps($pp, [ref]$caps)
    [G435HidProbe]::HidD_FreePreparsedData($pp) | Out-Null
    $hQuery.Close()

    Write-Host ""
    Write-Host "[2] TAMANHOS DE RELATORIOS DECLARADOS PELO WINDOWS (HidP_GetCaps)" -ForegroundColor Yellow
    if ($capsStatus -ne $HIDP_STATUS_SUCCESS) {
        Write-Host ("    Erro: HidP_GetCaps retornou status 0x{0:X8}." -f $capsStatus) -ForegroundColor Red
        continue
    }
    Write-Host ("    UsagePage               : 0x{0:X4}" -f $caps.UsagePage) -ForegroundColor Cyan
    Write-Host ("    Usage                   : 0x{0:X4}" -f $caps.Usage) -ForegroundColor Cyan
    Write-Host ("    InputReportByteLength   : {0} bytes" -f $caps.InputReportByteLength) -ForegroundColor Cyan
    Write-Host ("    OutputReportByteLength  : {0} bytes" -f $caps.OutputReportByteLength) -ForegroundColor Cyan
    Write-Host ("    FeatureReportByteLength : {0} bytes" -f $caps.FeatureReportByteLength) -ForegroundColor Cyan

    # Abrir handle para testes somente leitura
    $hRead = [G435HidProbe]::CreateFile($path, $GENERIC_READ, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($hRead.IsInvalid) {
        $hRead = [G435HidProbe]::CreateFile($path, 0, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, 0, [IntPtr]::Zero)
    }

    Write-Host ""
    Write-Host "[3] RESULTADO DO TESTE DE LEITURA DE INPUT REPORT (HidD_GetInputReport)" -ForegroundColor Yellow
    if ($hRead.IsInvalid) {
        Write-Host ("    Nao foi possivel abrir handle para teste (Win32Error={0})." -f $Marshal::GetLastWin32Error()) -ForegroundColor Red
    } elseif ($caps.InputReportByteLength -le 0) {
        Write-Host "    HidD_GetInputReport NAO executado: InputReportByteLength = 0." -ForegroundColor Gray
    } else {
        # Para o receptor Jieli 4C4A:4155 (sem Report IDs explicitos no descritor), testar apenas Report ID 0
        # utilizando um buffer exatamente do tamanho de InputReportByteLength (3 bytes).
        $inLen = [int]$caps.InputReportByteLength
        Write-Host ("    Parametros: ReportID = 0 | Tamanho do buffer = {0} bytes (InputReportByteLength exato) | 3 repeticoes" -f $inLen) -ForegroundColor Gray
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            $inReportBuf = New-Object byte[] $inLen
            $inReportBuf[0] = 0x00
            $okIn = [G435HidProbe]::HidD_GetInputReport($hRead, $inReportBuf, $inReportBuf.Length)
            $errIn = $Marshal::GetLastWin32Error()
            $hexIn = ($inReportBuf | ForEach-Object { "{0:X2}" -f $_ }) -join " "
            if ($okIn) {
                Write-Host ("    Tentativa #{0}: Retorno=True  | Win32Error=0  | Buffer=[ {1} ]" -f $attempt, $hexIn) -ForegroundColor Green
            } else {
                Write-Host ("    Tentativa #{0}: Retorno=False | Win32Error={1} | Buffer=[ {2} ]" -f $attempt, $errIn, $hexIn) -ForegroundColor DarkYellow
            }
            if ($attempt -lt 3) { Start-Sleep -Milliseconds 150 }
        }
        Write-Host "    Nota tecnica: Win32Error reflete o retorno da API/driver (hidclass.sys/hidusb.sys)." -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "[4] RESULTADO DOS TESTES DE FEATURE REPORT (HidD_GetFeature)" -ForegroundColor Yellow
    if ($caps.FeatureReportByteLength -eq 0) {
        Write-Host "    HidD_GetFeature NAO executado: FeatureReportByteLength = 0 declarado pelo Windows." -ForegroundColor Gray
        Write-Host "    (Esta colecao HID nao declara suporte a Feature Reports)." -ForegroundColor Gray
    } elseif (-not $hRead.IsInvalid) {
        $featLen = [int]$caps.FeatureReportByteLength
        $reportIdToTest = if ($isRealtekG435 -and $caps.UsagePage -eq 0xFFC0) { 6 } else { 0 }
        $featBuf = New-Object byte[] $featLen
        $featBuf[0] = [byte]$reportIdToTest

        Write-Host ("    Parametros: ReportID = {0} | Tamanho do buffer = {1} bytes (FeatureReportByteLength exato)" -f $reportIdToTest, $featLen) -ForegroundColor Gray
        $okFeat = [G435HidProbe]::HidD_GetFeature($hRead, $featBuf, $featBuf.Length)
        $errFeat = $Marshal::GetLastWin32Error()

        if ($okFeat) {
            $hexFeat = ($featBuf | ForEach-Object { "{0:X2}" -f $_ }) -join " "
            Write-Host ("    Resultado API HID : SUCESSO -> [ {0} ]" -f $hexFeat) -ForegroundColor Green
        } else {
            Write-Host ("    Resultado API HID : FALHOU (Retorno=False, Win32 GetLastError={0})" -f $errFeat) -ForegroundColor DarkYellow
        }
    }

    if (-not $hRead.IsInvalid) {
        $hRead.Close()
    }

    Write-Host ""
    Write-Host ("[5] RELATORIOS HID RECEBIDOS DURANTE O PERIODO DE CAPTURA ({0}s)" -f $CaptureSeconds) -ForegroundColor Yellow
    if ($caps.InputReportByteLength -gt 0 -and $CaptureSeconds -gt 0) {
        $hOv = [G435HidProbe]::CreateFile($path, $GENERIC_READ, ($FILE_SHARE_READ -bor $FILE_SHARE_WRITE), [IntPtr]::Zero, $OPEN_EXISTING, $FILE_FLAG_OVERLAPPED, [IntPtr]::Zero)
        if ($hOv.IsInvalid) {
            Write-Host ("    Nao foi possivel abrir handle overlapped para leitura (Win32Error={0})." -f $Marshal::GetLastWin32Error()) -ForegroundColor Red
        } else {
            $inLen = [int]$caps.InputReportByteLength
            Write-Host ("    Escutando Endpoint Interrupt IN com buffer exato de {0} bytes por {1} segundos..." -f $inLen, $CaptureSeconds) -ForegroundColor Gray
            Write-Host "    (Opcional: acione controles do headset ou aguarde para observar pacotes espontaneos)" -ForegroundColor Magenta

            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            $packetsCount = 0
            while ($sw.Elapsed.TotalSeconds -lt $CaptureSeconds) {
                $readBuf = New-Object byte[] $inLen
                $readErr = 0
                $bytesRead = [G435HidProbe]::ReadInterruptReport($hOv, $readBuf, 500, [ref]$readErr)
                $tStr = "{0:F2}s" -f $sw.Elapsed.TotalSeconds

                if ($bytesRead -gt 0) {
                    $packetsCount++
                    $hexPkt = ($readBuf[0..($bytesRead - 1)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
                    Write-Host ("    [{0}] Relatorio #{1} ({2} bytes): [ {3} ]" -f $tStr, $packetsCount, $bytesRead, $hexPkt) -ForegroundColor Green
                } elseif ($readErr -ne 0) {
                    Write-Host ("    [{0}] Erro ReadFile (Win32Error={1})" -f $tStr, $readErr) -ForegroundColor Red
                    break
                }
            }
            $hOv.Close()
            Write-Host ("    Total de relatorios HID recebidos na janela de {0}s: {1}" -f $CaptureSeconds, $packetsCount) -ForegroundColor Cyan
        }
    }
}

[G435HidProbe]::SetupDiDestroyDeviceInfoList($devInfo) | Out-Null

# ---- VERIFICACAO BLUETOOTH HFP (Abordagem GearHub: BTHENUM + {0000111E-...} + DEVPKEY_Device_BatteryLevel) ----
Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[6] VERIFICACAO BLUETOOTH HFP (DEVPKEY_Device_BatteryLevel / GearHub)" -ForegroundColor Yellow

$BatteryGuid      = [Guid]"104EA319-6EE2-4701-BD47-8DDBF425BBE5" # pid 2
$FriendlyNameGuid = [Guid]"A45C254E-DF1C-4EFD-8020-67D146A850E0" # pid 14
$DeviceDescGuid   = [Guid]"A45C254E-DF1C-4EFD-8020-67D146A850E0" # pid 2
$IsConnectedGuid  = [Guid]"83DA6326-97A6-4088-9453-A1923F573B29" # pid 15
$HfpServiceMarker = "{0000111E-0000-1000-8000-00805F9B34FB}"

$bthInstances = [G435HidProbe]::EnumerateBthEnumInstances()
$btG435Found = $false
$btG435Connected = $false
$btG435Percent = -1
$btG435Name = ""
$btG435Mac = ""

foreach ($inst in $bthInstances) {
    if ($inst -notlike "*$HfpServiceMarker*") { continue }

    # Extrair MAC de 12 digitos hexadecimais como no GearHub
    $sep = $inst.LastIndexOf('\')
    if ($sep -lt 0 -or ($sep + 1) -ge $inst.Length) { continue }
    $tail = $inst.Substring($sep + 1)
    $us = $tail.IndexOf('_')
    $before = if ($us -lt 0) { $tail } else { $tail.Substring(0, $us) }
    $amp = $before.LastIndexOf('&')
    $mac = if ($amp -lt 0) { $before } else { $before.Substring($amp + 1) }
    if ($mac.Length -ne 12) { continue }

    # Buscar no base BTHENUM\DEV_<MAC>
    $devMarker = "BTHENUM\DEV_$mac"
    $baseInst = $bthInstances | Where-Object { $_.StartsWith($devMarker, [System.StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
    $devName = ""
    $isConn = $false
    if ($baseInst) {
        $devName = [G435HidProbe]::ReadDevNodeStringProperty($baseInst, $FriendlyNameGuid, 14)
        if ([string]::IsNullOrWhiteSpace($devName)) {
            $devName = [G435HidProbe]::ReadDevNodeStringProperty($baseInst, $DeviceDescGuid, 2)
        }
        $connVal = [G435HidProbe]::ReadDevNodeByteProperty($baseInst, $IsConnectedGuid, 15)
        $isConn = ($connVal -eq 1)
    }

    $pct = [G435HidProbe]::ReadDevNodeByteProperty($inst, $BatteryGuid, 2)
    $isG435 = ($devName -match "(?i)\bg435\b")

    Write-Host ("    Instancia HFP : {0}" -f $inst) -ForegroundColor Gray
    Write-Host ("    Dispositivo   : `"{0}`" (MAC={1}, IsG435={2}, IsConnected={3})" -f $devName, $mac, $isG435, $isConn) -ForegroundColor White
    if ($pct -ge 0 -and $pct -le 100) {
        Write-Host ("    Bateria HFP   : {0}% (DEVPKEY_Device_BatteryLevel valida)" -f $pct) -ForegroundColor Green
    } else {
        Write-Host ("    Bateria HFP   : Indisponivel (retorno={0})" -f $pct) -ForegroundColor DarkYellow
    }

    if ($isG435) {
        $btG435Found = $true
        $btG435Name = $devName
        $btG435Mac = $mac
        $btG435Connected = $isConn
        if ($pct -ge 0 -and $pct -le 100) { $btG435Percent = $pct }
    }
}

if (-not $btG435Found) {
    Write-Host "    Nenhum Logitech G435 pareado/conectado via Bluetooth (BTHENUM HFP) encontrado neste momento." -ForegroundColor DarkYellow
}

Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[7] ESTADO UNIFICADO DO LOGITECH G435 (Key = 'g435')" -ForegroundColor Yellow
if ($btG435Found -and $btG435Connected) {
    $pctStr = if ($btG435Percent -ge 0) { "$btG435Percent%" } else { "Desconhecido" }
    Write-Host ("    Identidade Unica : Logitech G435 (key='g435')") -ForegroundColor Green
    Write-Host ("    Modo Ativo       : Bluetooth (HFP)") -ForegroundColor Cyan
    Write-Host ("    Porcentagem Real : {0}" -f $pctStr) -ForegroundColor Green
} elseif ($matchedDevices -gt 0) {
    $lastStr = if ($btG435Percent -ge 0) { "$btG435Percent% (ultimo valor lido via Bluetooth HFP) " } else { "" }
    Write-Host ("    Identidade Unica : Logitech G435 (key='g435')") -ForegroundColor Green
    Write-Host ("    Modo Ativo       : LIGHTSPEED") -ForegroundColor Cyan
    Write-Host ("    Estado Exibido   : {0}Bateria indisponivel via LIGHTSPEED" -f $lastStr) -ForegroundColor Yellow
} else {
    Write-Host "    Nenhum Logitech G435 ativo (nem via LIGHTSPEED nem via Bluetooth)." -ForegroundColor Red
}
Write-Host "======================================================================" -ForegroundColor Cyan
