# ec-test-app
Test app to exercise EC functionality through ACPI from the OS

## Environment Setup
Download recent EWDK and mount ISO
```
cd BuildEnv
setupbuildenv.cmd x86_arm64
```

## Compilation Instructions
To compile ectest.exe from exe folder in cmd with environment setup run
`msbuild ectest_app.vcxproj /p:Configuration=Debug /p:Platform=ARM64`

To compile ectest.sys from kmdf folder in cmd with environment setup run
`msbuild ectest_kmdf.vcxproj /p:Configuration=Debug /p:Platform=ARM64`

The driver needs ACPI entries to load and execute. Sample ACPI for loading the driver and stubbed implementation of fan is available in acpi folder.
If your ACPI already has fan and battery definitions you can just include ectest and add methods to expose the ACPI functions you want to test.

## Installing the driver and Running ectest.exe
After recompiling ACPI and booting your device you will need to install the driver and run the validation tests.
Copy the following files from output folders to a thumbdrive or location on the target to test:
```
ec-test-app\exe\arm64\Debug\ectest.exe
ec-test-app\kmdf\arm64\Debug\ectest_kmdf\*
<WDKROOT>\Program Files\Windows Kits\10\Tools\10.0.26100.0\arm64\devcon.exe
```

You can install the driver through device manager as well, but easier to use devcon in case you need to automate or you can DISM it into your image as well.
From admin command prompt on your target device cd to location of install files:
```
cd e:\install
devcon remove ACPI\ACPI1234
devcon install ectest.inf ACPI\ACPI1234
```

You will get a pop-up saying that the certificate is not tested and you can choose to install anyways. Otherwise if you install certificate in your certstore under tursted root you won't get this.
To run the test you can simply use the following
```
E:\>ectest -acpi \_SB.ECT0.TFST
Found matching Class GUID: ACPI\ACPI1234\0
\\.\GLOBALROOT\Device\00000016
DevicePath: \\.\GLOBALROOT\Device\00000016
Opened device successfully

Calling DeviceIoControl EVAL_ACPI_METHOD: \_SB.ECT0.TFST
ACPI Method:
  Signature: 0x426f6541
  Length: 0x30
  Count: 0x3
    Argument[0]:
    Integer Value: 0x0
    Argument[1]:
    Integer Value: 0x1
    Argument[2]:
    Integer Value: 0x2
ACPI Raw Output:
 0x41 0x65 0x6f 0x42 0x30 0x0 0x0 0x0 0x3 0x0 0x0 0x0 0x0 0x0 0x8 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x8 0x0 0x1 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x8 0x0 0x2 0x0 0x0 0x0 0x0 0x0 0x0 0x0
```

You can add more functions in the ectest.asl file to add more test functions to your ACPI that calls other ACPI methods and just pass in the name of your new test method on the command line.