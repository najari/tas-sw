param(
  [string]$Action = 'windows',
  [string]$Name = '',
  [int]$X = 0,
  [int]$Y = 0,
  [string]$Text = '',
  [int]$Wait = 300
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$src = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NativeUI {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder sb, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(150);
    mouse_event(LEFTDOWN, 0, 0, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(80);
    mouse_event(LEFTUP, 0, 0, 0, IntPtr.Zero);
  }
}
"@

Add-Type -TypeDefinition $src

function Get-WindowsInternal {
  $list = New-Object System.Collections.ArrayList
  $cb = [NativeUI+EnumWindowsProc]{
    param($hWnd, $lParam)
    if ([NativeUI]::IsWindowVisible($hWnd)) {
      $sb = New-Object System.Text.StringBuilder 512
      [void][NativeUI]::GetWindowTextW($hWnd, $sb, 512)
      $t = $sb.ToString()
      if ($t.Trim().Length -gt 0) {
        $sb2 = New-Object System.Text.StringBuilder 256
        [void][NativeUI]::GetClassNameW($hWnd, $sb2, 256)
        [uint32]$pid2 = 0
        [void][NativeUI]::GetWindowThreadProcessId($hWnd, [ref]$pid2)
        $r = New-Object NativeUI+RECT
        [void][NativeUI]::GetWindowRect($hWnd, [ref]$r)
        $pname = ''
        try { $pname = (Get-Process -Id $pid2 -ErrorAction SilentlyContinue).ProcessName } catch {}
        [void]$list.Add([pscustomobject]@{ Handle=$hWnd; PID=$pid2; Process=$pname; Title=$t; Class=$sb2.ToString(); Rect=("{0},{1},{2},{3}" -f $r.Left,$r.Top,$r.Right,$r.Bottom) })
      }
    }
    return $true
  }
  [void][NativeUI]::EnumWindows($cb, [IntPtr]::Zero)
  return $list
}

switch ($Action) {
  'windows' { Get-WindowsInternal | Format-Table -AutoSize | Out-String -Width 400 }
  'shot' {
    $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
    $path = "C:\work\tas_server_analysis\mcds\$Name.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    "saved $path (virtual screen $($b.Width)x$($b.Height) at $($b.X),$($b.Y))"
  }
  'crop' {
    # capture rect given by X Y (top-left) and Text "WxH"
    $wh = $Text -split 'x'
    $w = [int]$wh[0]; $h = [int]$wh[1]
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($X, $Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $path = "C:\work\tas_server_analysis\mcds\$Name.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    "saved $path crop ${w}x${h} at $X,$Y"
  }
  'click' {
    [NativeUI]::Click($X, $Y)
    Start-Sleep -Milliseconds $Wait
    "clicked $X,$Y"
  }
  'activate' {
    $w = Get-WindowsInternal | Where-Object { $_.Title -like "*$Name*" } | Select-Object -First 1
    if ($w) { [void][NativeUI]::SetForegroundWindow($w.Handle); "activated: $($w.Title)" } else { "not found: $Name" }
  }
  'key' {
    [System.Windows.Forms.SendKeys]::SendWait($Text)
    Start-Sleep -Milliseconds $Wait
    "sent: $Text"
  }
}
