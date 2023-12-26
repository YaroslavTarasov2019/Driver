#include "Header.h"


VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    LARGE_INTEGER interval = { 0 };
    PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
    interval.QuadPart = -10 * 1000 * 1000;
    while (DeviceObject) {
        IoDetachDevice(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice);
        DeviceObject = DeviceObject->NextDevice;
    }
    while (pendingkey) {
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }
    DeviceObject = DriverObject->DeviceObject;
    while (DeviceObject) {
        IoDeleteDevice(DeviceObject);
        DeviceObject = DeviceObject->NextDevice;
    }

    KdPrint(("Unload Our Driver\r\n"));
}

NTSTATUS DispatchPass(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice, Irp);
}

VOID GenerateNewSignals(PKEYBOARD_INPUT_DATA Keys, INT j, INT* structnum, CHAR* keyflag[4])
{
    for (INT i = 0; i < 4; i++)
    {
        if (CHOOSE_TABLE_CODE == ASCII_TABLE)     Keys[i].MakeCode = (i == 0 || i == 3) ? gMappingTable[j].asciiCode : gMappingTable[j].asciiCode2;
        else if (CHOOSE_TABLE_CODE == UNICODE_TABLE)  Keys[i].MakeCode = (i == 0 || i == 3) ? gMappingTable[j].uniCode : gMappingTable[j].uniCode2;
        else if (CHOOSE_TABLE_CODE == USER_TABLE)  Keys[i].MakeCode = (i == 0 || i == 3) ? gMappingTable[j].userCode : gMappingTable[j].userCode2;

        Keys[i].UnitId = KbdDeviceID;
        Keys[i].Flags = (i < 2) ? 0 : 1;
        
        KdPrint(("Input: Scan code %x (%s)\r\n", Keys[i].MakeCode, keyflag[Keys[i].Flags]));
    }
    *structnum += 3;
}

VOID Change_Table_Code()
{
    if (CHOOSE_TABLE_CODE == USER_TABLE) CHOOSE_TABLE_CODE = ORIGINAL_TABLE;
    else if (CHOOSE_TABLE_CODE == ASCII_TABLE) CHOOSE_TABLE_CODE = UNICODE_TABLE;
    else if (CHOOSE_TABLE_CODE == UNICODE_TABLE) CHOOSE_TABLE_CODE = USER_TABLE;
    else { CHOOSE_TABLE_CODE++; }
    KdPrint(("F7 Pressed! Global Int increased to: %d\n", CHOOSE_TABLE_CODE));
}

VOID Change_Signal(PKEYBOARD_INPUT_DATA Keys, INT j, INT structnum, CHAR* keyflag[4], PIRP Irp)
{
    if (CHOOSE_TABLE_CODE == ASCII_TABLE && gMappingTable[j].asciiCode2 == 0x00)
    {
        Keys[0].MakeCode = gMappingTable[j].asciiCode;
        KdPrint(("Input: Scan code %x (%s)\r\n", Keys[0].MakeCode, keyflag[Keys[0].Flags]));
    }
    else if (CHOOSE_TABLE_CODE == UNICODE_TABLE && gMappingTable[j].uniCode2 == 0x00)
    {
        Keys[0].MakeCode = gMappingTable[j].uniCode;
        KdPrint(("Input: Scan code %x (%s)\r\n", Keys[0].MakeCode, keyflag[Keys[0].Flags]));
    }
    else if (CHOOSE_TABLE_CODE == USER_TABLE && gMappingTable[j].userCode2 == 0x00)
    {
        Keys[0].MakeCode = gMappingTable[j].userCode;
        KdPrint(("Input: Scan code %x (%s)\r\n", Keys[0].MakeCode, keyflag[Keys[0].Flags]));
    }
    else
    {
        if (Keys[0].Flags == 0)
        {
            GenerateNewSignals(Keys, j, &structnum, keyflag);
            Irp->IoStatus.Information = structnum * sizeof(KEYBOARD_INPUT_DATA);
        }
    }
}

NTSTATUS ReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);
    CHAR* keyflag[4] = { "KeyDown", "KeyUp", "E0", "E1" };

    PKEYBOARD_INPUT_DATA Keys = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
    int structnum = (int)Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);

    if (Irp->IoStatus.Status == STATUS_SUCCESS && structnum == 1) {
        if (Keys[0].MakeCode == MODE_SWITCH && strcmp(keyflag[Keys[0].Flags], "KeyUp") == 0) {
            if (KbdDeviceID == 99)  KbdDeviceID = Keys[0].UnitId;
            Change_Table_Code();
        }
        else
        {
            switch (CHOOSE_TABLE_CODE)
            {
                case ORIGINAL_TABLE:
                    KdPrint(("Input: Scan code %x (%s)\r\n", Keys[0].MakeCode, keyflag[Keys[0].Flags]));
                    break;
                case ASCII_TABLE:
                    for (ULONG j = 0; j < ARRAYSIZE(gMappingTable); ++j) {
                        if (gMappingTable[j].scanCode == Keys[0].MakeCode) {
                            Change_Signal(Keys, j, structnum, keyflag, Irp);
                            break;
                        }
                    }
                    break;
                case UNICODE_TABLE:
                    for (ULONG j = 0; j < ARRAYSIZE(gMappingTable); ++j) {
                        if (gMappingTable[j].scanCode == Keys[0].MakeCode) {
                            Change_Signal(Keys, j, structnum, keyflag, Irp);
                            break;
                        }
                    }
                    break;
                case USER_TABLE:
                    for (ULONG j = 0; j < ARRAYSIZE(gMappingTable); ++j) {
                        if (gMappingTable[j].scanCode == Keys[0].MakeCode) {
                            Change_Signal(Keys, j, structnum, keyflag, Irp);
                            break;
                        }
                    }
                    break;
            }
        }
    }

    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }
    pendingkey--;
    return Irp->IoStatus.Status;
}

NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
    pendingkey++;
    return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice, Irp);
}

NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS status;
    UNICODE_STRING MCName = RTL_CONSTANT_STRING(L"\\Driver\\kbdclass");
    PDRIVER_OBJECT targetDriverObject = NULL;
    PDEVICE_OBJECT currentDeviceObject = NULL;
    PDEVICE_OBJECT myDeviceObject = NULL;

    //// Отримання об'єкта драйвера для \\Driver\\kbdclass
    status = ObReferenceObjectByName(&MCName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&targetDriverObject);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Reference failed \r\n"));
        return status;
    }
    
    // Отримання першого пристрою в стеку пристроїв драйвера
    currentDeviceObject = targetDriverObject->DeviceObject;
    while (currentDeviceObject) {
        // Створення нового пристрою
        status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), NULL, currentDeviceObject->DeviceType, 0, FALSE, &myDeviceObject);
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(targetDriverObject);
            return status;
        }

        // Ініціалізація даних розширення пристрою
        RtlZeroMemory(myDeviceObject->DeviceExtension, sizeof(DEVICE_EXTENSION));

        // Приєднання пристрою до стеку пристроїв
        status = IoAttachDeviceToDeviceStackSafe(myDeviceObject, currentDeviceObject, &((PDEVICE_EXTENSION)myDeviceObject->DeviceExtension)->LowerKbdDevice);
        if (!NT_SUCCESS(status)) {
            IoDeleteDevice(myDeviceObject);
            ObDereferenceObject(targetDriverObject);
            return status;
        }

        // Налаштування прапорів пристрою
        myDeviceObject->Flags |= DO_BUFFERED_IO;
        myDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        // Перехід до наступного пристрою в стеку
        currentDeviceObject = currentDeviceObject->NextDevice;
    }

    // Зменшення лічильника посилань на об'єкт драйвера
    ObDereferenceObject(targetDriverObject);

    return STATUS_SUCCESS;
}


NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    int i;
    DriverObject->DriverUnload = DriverUnload;
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = DispatchPass;
    }
    DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;
    KdPrint(("driver is loaded\r\n"));
    status = MyAttachDevice(DriverObject);
    if (!NT_SUCCESS(status)) {
        KdPrint(("attaching is failing\r\n"));
    }
    else
    {
        KdPrint(("attaching succeeds\r\n"));
    }
    ReadFile(L"\\??\\C:\\Driver\\settings.txt");
    return status;
}

NTSTATUS ReadFile(PCWSTR filePath)
{
    UNICODE_STRING fileUnicodeString;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK ioStatusBlock;
    HANDLE fileHandle;
    CHAR buffer[2000];

    RtlInitUnicodeString(&fileUnicodeString, filePath);
    InitializeObjectAttributes(&objectAttributes, &fileUnicodeString, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS status = ZwOpenFile(&fileHandle, GENERIC_READ, &objectAttributes, &ioStatusBlock, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);

    if (NT_SUCCESS(status))
    {
        status = ZwReadFile(fileHandle, NULL, NULL, NULL, &ioStatusBlock, buffer, sizeof(buffer), NULL, NULL);
        if (NT_SUCCESS(status))     ProcessFileContents(buffer, ioStatusBlock.Information);
        ZwClose(fileHandle);
    }
    else
    {
        KdPrint(("Failed to open file. Status: %x\n", status));
    }

    return status;
}

VOID ProcessFileContents(PCHAR buffer, ULONG_PTR bufferSize)
{
    CHAR temp1[4] = { 0 };
    CHAR temp2[4] = { 0 };
    INT iter = 0;
    INT wordNumber = 1;
    INT number = 0;

    for (ULONG i = 0; i < bufferSize; ++i)
    {
        if (buffer[i] == ' ')
        {
            iter = 0;
            wordNumber++;
        }
        else if (buffer[i] == '\r')
        {
            gMappingTable[number].userCode = HexCharsToInt(temp1[2], temp1[3]);
            gMappingTable[number].userCode2 = HexCharsToInt(temp2[2], temp2[3]);
            number++;

            iter = 0;
            temp1[0] = temp1[1] = temp1[2] = temp1[3] = ' ';
            temp2[0] = temp2[1] = temp2[2] = temp2[3] = ' ';
            wordNumber = 1;
        }
        else if (buffer[i] != '\n')
        {
            if (wordNumber == 1) temp1[iter] = buffer[i];
            else if (wordNumber == 2) temp2[iter] = buffer[i];
            iter++;
        }
    }
}

USHORT HexCharsToInt(CHAR ch1, CHAR ch2)
{
    return FromCharToInt(ch1) * 16 + FromCharToInt(ch2);
}

USHORT FromCharToInt(CHAR temp)
{
    switch (toupper(temp))
    {
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'A': return 10;
    case 'B': return 11;
    case 'C': return 12;
    case 'D': return 13;
    case 'E': return 14;
    case 'F': return 15;
    default: return 0;
    }
}
