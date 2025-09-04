
declare const enum HKEY {
	CLASSES_ROOT				= 0x80000000,
	CURRENT_USER				= 0x80000001,
	LOCAL_MACHINE				= 0x80000002,
	USERS						= 0x80000003,
	PERFORMANCE_DATA			= 0x80000004,
	PERFORMANCE_TEXT			= 0x80000050,
	PERFORMANCE_NLSTEXT			= 0x80000060,
	CURRENT_CONFIG				= 0x80000005,
	DYN_DATA					= 0x80000006,
	CURRENT_USER_LOCAL_SETTINGS	= 0x80000007,
}

declare const enum TYPE {
	NONE						= 0x00000000, // No value type
	SZ							= 0x00000001, // Unicode nul terminated string
	EXPAND_SZ				 	= 0x00000002, // Unicode nul terminated string (with environment variable references)
	BINARY						= 0x00000003, // Free form binary
	DWORD					 	= 0x00000004, // 32-bit number
	DWORD_BIG_ENDIAN			= 0x00000005, // 32-bit number
	LINK						= 0x00000006, // Symbolic Link (unicode)
	MULTI_SZ					= 0x00000007, // Multiple Unicode strings
	RESOURCE_LIST			 	= 0x00000008, // Resource list in the resource map
	FULL_RESOURCE_DESCRIPTOR	= 0x00000009, // Resource list in the hardware description
	RESOURCE_REQUIREMENTS_LIST	= 0x0000000A,
	QWORD					 	= 0x0000000B, // 64-bit number
}

declare const enum SAM {
	DELETE						= 0x00010000,
	READ_CONTROL				= 0x00020000,
	WRITE_DAC					= 0x00040000,
	WRITE_OWNER					= 0x00080000,
	SYNCHRONIZE					= 0x00100000,

	KEY_QUERY_VALUE				= 0x0001,
	KEY_SET_VALUE				= 0x0002,
	KEY_CREATE_SUB_KEY			= 0x0004,
	KEY_ENUMERATE_SUB_KEYS		= 0x0008,
	KEY_NOTIFY					= 0x0010,
	KEY_CREATE_LINK				= 0x0020,
	KEY_WOW64_64KEY				= 0x0100,
	KEY_WOW64_32KEY				= 0x0200,

	KEY_READ					= READ_CONTROL | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY,
	KEY_WRITE					= READ_CONTROL | KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
	KEY_EXECUTE					= KEY_READ,
	KEY_ALL_ACCESS				= DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | KEY_CREATE_LINK,
}

// Open/Create Options
declare const enum OPTIONS {
	NON_VOLATILE		= 0x00000000,	// Key is preserved
	VOLATILE			= 0x00000001,	// Key is not preserved when system is rebooted
	CREATE_LINK			= 0x00000002,	// Created key is a symbolic link
	BACKUP_RESTORE		= 0x00000004,	// open for backup or restore special access rules privilege required
	OPEN_LINK			= 0x00000008,	// Open symbolic link
	DONT_VIRTUALIZE		= 0x00000010,	// Disable Open/Read/Write virtualization for this open and the resulting handle.

	LEGAL_CREATE		= NON_VOLATILE | VOLATILE | CREATE_LINK | BACKUP_RESTORE | OPEN_LINK | DONT_VIRTUALIZE,
	LEGAL_OPEN			= BACKUP_RESTORE | OPEN_LINK | DONT_VIRTUALIZE,
}

// for GetValue
declare const enum RRF {
	TYPE_NONE			= 0x00000001,	// restrict type to TYPE.NONE
	TYPE_SZ				= 0x00000002,	// restrict type to TYPE.SZ			(automatically converts REG_EXPAND_SZ to REG_SZ unless RRF_NOEXPAND is specified)
	TYPE_EXPAND_SZ		= 0x00000004,	// restrict type to TYPE.EXPAND_SZ	(must specify NOEXPAND or GetValue will fail with ERROR_INVALID_PARAMETER)
	TYPE_BINARY			= 0x00000008,	// restrict type to TYPE.BINARY
	TYPE_DWORD			= 0x00000010,	// restrict type to TYPE.DWORD
	TYPE_MULTI_SZ		= 0x00000020,	// restrict type to TYPE.MULTI_SZ
	TYPE_QWORD			= 0x00000040,	// restrict type to TYPE.QWORD
	TYPE_32BIT			= TYPE_BINARY | TYPE_DWORD, // restrict type to *32-bit* TYPE.BINARY or TYPE.DWORD
	TYPE_64BIT			= TYPE_BINARY | TYPE_QWORD, // restrict type to *64-bit* TYPE.BINARY or TYPE.QWORD
	RT_ANY			 	= 0x0000ffff,	// no type restriction
	SUBKEY_WOW6464KEY	= 0x00010000,	// when opening the subkey (if provided) force open from the 64bit location
	SUBKEY_WOW6432KEY	= 0x00020000,	// when opening the subkey (if provided) force open from the 32bit location
	NOEXPAND			= 0x10000000,	// do not automatically expand environment strings if value is of type REG_EXPAND_SZ
	ZEROONFAILURE		= 0x20000000,	// if pvData is not NULL, set content to all zeros on failure
}


//-----------------------------------------------------------------------------
// keys
//-----------------------------------------------------------------------------

export type Subkey = string | null | undefined;

export interface KeyResult {
	name: string;
	class: string;
	last_write_time: Date;
}

export function CreateKey(h: number, subkey: string, options: number, sam: number, class_name?: string): number;
export function OpenKey(h: number, subkey: string, options: number, sam: number): number;
export function CloseKey(h: number): number;
export function RenameKey(h: number, subkey: string, newname: string): number;
export function DeleteKey(h: number, subkey: string, sam: number): number;
export function DeleteTree(h: number, subkey: string): number;
export function DeleteTreeAsync(h: number, subkey: string): Promise<number>;
export function ConnectRegistry(machine: string, h: number): number;
export function ConnectRegistryAsync(machine: string, h: number): Promise<number>;

export function QueryKey(h: number, subkey?: Subkey, class_data?: Uint16Array): {
	status:					number,
	class_len:				number,
	subkeys:				number,
	max_subkey_len:			number,
	max_class_len:			number,
	values:					number,
	max_value_name_len:		number,
	max_value_len:			number,
	security_descriptor:	number,
	last_write_time:		Date,
};

export function EnumKey(h: number, i: number, name_buffer: Uint16Array, class_buffer?: Uint16Array): KeyResult;
export function EnumKeys(h: number, subkey: Subkey): KeyResult[];
export function EnumKeysAsync(h: number, subkey: Subkey): Promise<KeyResult[]>;

//-----------------------------------------------------------------------------
// values
//-----------------------------------------------------------------------------

export interface ValueResult {
	name: string;
	type: number,
	data: ArrayBuffer,
}

export function DeleteValue(h: number, subkey: Subkey, value: string): number;
export function GetValue(h: number, subkey: Subkey, value: string, flags: number, data?: Uint8Array): {
	status: number,
	type: number,
	size: number
};
export function SetValue(h: number, subkey: Subkey, value: string, type: number, data: Uint8Array): number;
export function EnumValue(h: number, i: number, name_buffer: Uint16Array, data_buffer: Uint8Array): {
	status: number,
	name: string,
	type: number,
	size: number,
};

export function EnumValues(h: number, subkey: Subkey): ValueResult[];
export function EnumValuesMulti(h: number, subkey: Subkey, values: string[]):	ValueResult[];
export function EnumValuesAsync(h: number, subkey: Subkey): Promise<ValueResult[]>;

//-----------------------------------------------------------------------------
// native values
//-----------------------------------------------------------------------------

export type Value = undefined | string | number | string[] | Uint8Array | ArrayBuffer;

export function ToNative(type: number, data: ArrayBuffer): Value;
export function FromNative(value: Value): {type: number, data: Uint8Array};
export function GetValue2(h: number, subkey: Subkey, value: string, flags: number): Value;
export function SetValue2(h: number, subkey: Subkey, value: string, data: Value): number;

