#include <windows.h>
#include <stdio.h>
#include <node_api.h>
#include <delayimp.h>
#include <array>
#include "node.h"
#include "reg-string.h"

#if 1
extern "C" const IMAGE_DOS_HEADER __ImageBase;
template<class T> const T* fromRva(RVA rva) { return (const T*)((const char*)&__ImageBase + rva); }

extern "C" FARPROC WINAPI __delayLoadHelper2(const ImgDelayDescr *pidd, FARPROC *dest) {
	HMODULE hmod	= *fromRva<HMODULE>(pidd->rvaHmod);
	FARPROC func	= NULL;

	auto dllname	= fromRva<char>(pidd->rvaDLLName);

	if (_stricmp(dllname, "node.exe") == 0) {
		auto index = (const IMAGE_THUNK_DATA*)dest - fromRva<IMAGE_THUNK_DATA>(pidd->rvaIAT);
		auto entry = fromRva<IMAGE_THUNK_DATA>(pidd->rvaINT) + index;

		union {
			LPCSTR	name;
			DWORD	ordinal;
		} dlp;

		if (!IMAGE_SNAP_BY_ORDINAL(entry->u1.Ordinal))
			dlp.name	= fromRva<IMAGE_IMPORT_BY_NAME>(entry->u1.AddressOfData)->Name;
		else
			dlp.ordinal	= IMAGE_ORDINAL(entry->u1.Ordinal);

		hmod	= GetModuleHandle(NULL);
		func	= GetProcAddress(hmod, dlp.name);
		*dest	= func;
	}
	return func;
}
#else
static FARPROC WINAPI load_exe_hook(unsigned int event, DelayLoadInfo* info) {
	if (event == dliNotePreLoadLibrary && _stricmp(info->szDll, "node.exe") == 0)
		return (FARPROC)GetModuleHandle(NULL);
	return NULL;
}

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = load_exe_hook;
#endif

napi_async_context	async_context;

template<typename C, size_t N> struct fixed_string {
	C s[N];
	fixed_string() 				{ s[0] = 0; }
	fixed_string(const C *c)	{ strcpy(s, c); }
	operator const C*() const	{ return s; }
	auto& operator=(const C *c) { strcpy(s, c); return *this; }
   	auto	begin()		const	{ return s; }
};

namespace Node {
	template<> struct node_type<HKEY> {
		static napi_value to_value(HKEY h)			{ return number((uint32_t)(uint64_t)h); }
		static HKEY from_value(napi_value x)		{ return (HKEY)(uint64_t)(uint32_t)number(x); }
	};

	template<> struct node_type<::string> {
		static napi_value to_value(const ::string &x) {
			return string((char16_t*)x.begin());
		}
		static auto from_value(napi_value x) {
			if (auto s = string::is(x)) {
				auto len = s.length() + 1;
				alloc_block<char16_t> buffer(len);
				s.get_utf16(buffer.begin(), len);
				return ::string((wchar_t*)buffer.detach(), ::string::pre_alloc);
			}
			return ::string();
		}
	};

	template<> struct node_type<LPCWSTR> {
		static napi_value to_value(LPCWSTR x) {
			return x ? string((char16_t*)x) : null;
		}
		static auto from_value(napi_value x) {
			return Node::from_value<::string>(x);
			/*
			fixed_string<char16_t, 100> fs;
			if (auto s = string::is(x))
				s.get_utf16(fs.s, 100);

			return reinterpret_cast<fixed_string<wchar_t, 100>&>(fs);
			*/
		}
	};

	template<> struct node_type<FILETIME> {
		static napi_value to_value(FILETIME x) {
			return Date(((((uint64_t)x.dwHighDateTime << 32) | x.dwLowDateTime) - 116444736000000000ULL) / 10000);
		}
	};
}

Node::value ToNative(DWORD type, range<uint8_t*> data) {
	switch (type) {
		case REG_NONE:
			return Node::value(Node::undefined);
		case REG_SZ:
		case REG_EXPAND_SZ:
			return Node::string((const char16_t*)data.begin(), data.size() / sizeof(char16_t) - 1); // Exclude null terminator
		case REG_MULTI_SZ: {
			Node::array result;
			auto data2 = make_range((const char16_t*)data.begin(), (const char16_t*)data.end());
			while (data2.size()) {
				auto a = data2.begin();
				auto b = data2.find(0);
				if (a == b)
					break; // Empty string, stop processing
				result.push(Node::string(a, b - a));
				data2 = data2.slice(b - a + 1);
			}
			return result;
		}
		case REG_DWORD: 
			return Node::number(*(DWORD*)data.begin());
		case REG_DWORD_BIG_ENDIAN: 
			return Node::number(*(DWORD*)data.begin());
		case REG_QWORD: 
			return Node::bigint(*(uint64_t*)data.begin());
		default: {
			uint8_t *copy;
			auto buffer = Node::TypedArray<uint8_t>(data.size(), &copy);
			memcpy(copy, data.begin(), data.size());
			return buffer;
		}
	}
}

struct Value {
	DWORD					type;
	Node::TypedArray<byte>	data;

	Value(DWORD type, Node::TypedArray<byte> data) : type(type), data(data) {}
	Value() : type(REG_NONE), data(0, nullptr) {}
};

template<typename T> auto make_data(const T &value) {
	byte *raw;
	auto data = Node::TypedArray<byte>(sizeof(T), &raw);
	memcpy(data.native().begin(), &value, sizeof(T));
	return data;
}
template<typename T> auto make_data2(range<T*> &value) {
	auto size = value.size() * sizeof(T);
	byte *raw;
	auto data = Node::TypedArray<byte>(size, &raw);
	memcpy(data.native().begin(), value.begin(), size);
	return data;
//	return Node::TypedArray<byte>(Node::ArrayBuffer(value.detach(), size, [](void *data) { free(data); }), 0, size);
}

Value FromNative(Node::value data) {
	switch (data.type()) {
		case napi_undefined:
		case napi_null:
			return Value();

		case napi_boolean:
			return Value(REG_DWORD, make_data((uint32_t)Node::boolean(data) ? 1 : 0));

		case napi_number: {
			auto num = Node::number(data);
			if (num < double(UINT32_MAX))
				return Value(REG_DWORD, make_data((uint32_t)num));
			return Value(REG_QWORD, make_data((int64_t)num));
		}
		case napi_bigint: {
			auto num = Node::bigint(data);
			return Value(REG_QWORD, make_data((int64_t)num));
		}

		case napi_string: {
			auto str = Node::string(data);
			auto len = str.length() + 1;
			growing_block<char16_t> buffer(len);
			str.get_utf16(buffer.begin(), len);
			return Value(REG_SZ, make_data2(buffer));
		}

		case napi_object:
			if (auto buffer = Node::ArrayBuffer::is(data)) {
				return Value(REG_BINARY, Node::TypedArray<byte>(buffer, 0, buffer.native().size()));

			} else if (auto buffer = Node::TypedArray<uint8_t>::is(data)) {
				return Value(REG_BINARY, buffer);

			} else if (auto array = Node::array::is(data)) {
				growing_block<char16_t> buffer;
				for (const Node::value item : array) {
					if (auto str = Node::string::is(item)) {
						auto len	= str.length() + 1;
						str.get_utf16(buffer.alloc(len), len);
					}
				}
				
				*buffer.alloc(1) = 0; // Null-terminate the multi-string
				buffer.finalize();
				return Value(REG_MULTI_SZ, make_data2(buffer));
			}
			break;

		default:
			break;
	}

	return Value();
}

namespace Node {
	template<> struct node_type<Value> {
		static napi_value to_value(Value v) {
			return object::make(
				"type", v.type,
				"data", v.data
			);
		}
		static Value from_value(napi_value x) {
			return FromNative(value(x));
		}
	};
}
//-----------------------------------------------------------------------------
// keys
//-----------------------------------------------------------------------------

auto CreateKey(HKEY h, LPCWSTR subkey, DWORD options, REGSAM sam, LPCWSTR class_name) {
	HKEY result;
	RegCreateKeyExW(h, subkey, 0, (LPWSTR)class_name, options, sam, nullptr, &result, nullptr);
	return result;
}

auto OpenKey(HKEY h, LPCWSTR subkey, DWORD options, REGSAM sam) {
	HKEY result;
	RegOpenKeyExW(h, subkey, options, sam, &result);
	return result;
}

auto CloseKey(HKEY h) {
	return RegCloseKey(h);
}

auto RenameKey(HKEY h, LPCWSTR lpSubKeyName, LPCWSTR lpNewKeyName) {
	return RegRenameKey(h, lpSubKeyName, lpNewKeyName);
}

auto DeleteKey(HKEY h, LPCWSTR lpSubKey, REGSAM sam) {
	return RegDeleteKeyExW(h, lpSubKey, sam, 0);
}

auto DeleteTree(HKEY h, LPCWSTR lpSubKey) {
	return RegDeleteTreeW(h, lpSubKey);
}

auto DeleteTreeAsync(HKEY h, string lpSubKey) {
	Node::Promise promise;
	Node::async_work("DeleteTree",
		[h, lpSubKey]() {
			return RegDeleteTreeW(h, lpSubKey);
		},
		[promise](napi_status status, LSTATUS result) {
			promise.resolve(result);
		}
	);
	return promise;
}

auto QueryKey(HKEY h, LPCWSTR subkey, range<uint16_t*> class_data) {
	DWORD	class_len 			= class_data.size();
	DWORD	subkeys 			= 0;
	DWORD	max_subkey_len 		= 0;
	DWORD	max_class_len 		= 0;
	DWORD	values 				= 0;
	DWORD	max_value_name_len 	= 0;
	DWORD	max_value_len 		= 0;
	DWORD	security_descriptor = 0;
	FILETIME last_write_time	= {};

	auto h0		= h;
	auto status	= RegOpenKeyExW(h0, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &h);
	if (status == ERROR_SUCCESS) {
		status = RegQueryInfoKeyW(h,
			(wchar_t*)class_data.begin(), &class_len, nullptr,
			&subkeys, &max_subkey_len, &max_class_len,
			&values, &max_value_name_len, &max_value_len,
			&security_descriptor,
			&last_write_time
		);

		if (h != h0)
			RegCloseKey(h);
	}

	return Node::object::make(
		"status",				status,
		"class_len",			class_len,
		"subkeys",				subkeys,
		"max_subkey_len", 		max_subkey_len,
		"max_class_len", 		max_class_len,
		"values",				values,
		"max_value_name_len", 	max_value_name_len,
		"max_value_len", 		max_value_len,
		"security_descriptor", 	security_descriptor,
		"last_write_time", 		last_write_time
	);
}

auto EnumKey(HKEY h, int index, range<uint16_t*> name_buffer, range<uint16_t*> class_buffer) {
	DWORD	name_len			= name_buffer.size();
	DWORD	class_len			= class_buffer.size();
	FILETIME last_write_time	= {};
	auto status	= RegEnumKeyExW(h, index, (wchar_t*)name_buffer.begin(), &name_len, nullptr, (wchar_t*)class_buffer.begin(), &class_len, &last_write_time);
	return Node::object::make(
		"name",				Node::string((char16_t*)name_buffer.begin(), name_len), // Exclude null terminator
		"class",			Node::string((char16_t*)class_buffer.begin(), class_len), // Exclude null terminator
		"last_write_time",	last_write_time
	);
}

auto EnumKeys(HKEY h, LPCWSTR subkey) {
	Node::array result;

	auto h0		= h;
	auto status	= RegOpenKeyExW(h0, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &h);

	if (status == ERROR_SUCCESS) {
		DWORD	subkeys			= 0;
		DWORD	max_subkey_len	= 0;
		DWORD	max_class_len 	= 0;
		status = RegQueryInfoKeyW(h,
			nullptr, nullptr, nullptr,
			&subkeys, &max_subkey_len, &max_class_len,
			nullptr, nullptr, nullptr,
			nullptr, nullptr
		);

		++max_subkey_len; // +1 for null terminator
		++max_class_len; // +1 for null terminator
		auto name_buffer = (wchar_t*)malloc(max_subkey_len * sizeof(wchar_t));
		auto class_buffer = (wchar_t*)malloc(max_class_len * sizeof(wchar_t));

		for (int i = 0; i < subkeys; ++i) {
			DWORD	name_len			= max_subkey_len;
			DWORD	class_len			= max_class_len;
			FILETIME last_write_time	= {};
			auto status	= RegEnumKeyExW(h, i, name_buffer, &name_len, nullptr, class_buffer, &class_len, &last_write_time);
			result.push(Node::object::make(
				"name",				Node::string((char16_t*)name_buffer, name_len), // Exclude null terminator
				"class",			Node::string((char16_t*)class_buffer, class_len), // Exclude null terminator
				"last_write_time",	last_write_time
			));
		}

		free(name_buffer);
		free(class_buffer);
		if (h != h0)
			RegCloseKey(h);
	}
	return result;
}

auto EnumKeysAsync(HKEY h, string subkey) {
	Node::Promise	promise;

	HKEY h1;
	auto status	= RegOpenKeyExW(h, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &h1);
	if (status != ERROR_SUCCESS) {
		promise.reject(status);
		return promise;
	}

	DWORD	subkeys			= 0;
	DWORD	max_subkey_len	= 0;
	DWORD	max_class_len 	= 0;
	status = RegQueryInfoKeyW(h1,
		nullptr, nullptr, nullptr,
		&subkeys, &max_subkey_len, &max_class_len,
		nullptr, nullptr, nullptr,
		nullptr, nullptr
	);

	++max_subkey_len;	// +1 for null terminator
	++max_class_len;	// +1 for null terminator

	auto worker = Node::async_work("EnumKeys",
		[h, h1, subkeys, max_subkey_len, max_class_len]() {
			struct Entry {
				string	name;
				string	class_name;
				FILETIME last_write_time;
			};

			alloc_block<wchar_t> name_buffer(max_subkey_len);
			alloc_block<wchar_t> class_buffer(max_class_len);
			alloc_block<Entry> entries(subkeys);

			for (int i = 0; i < subkeys; ++i) {
				auto	&entry		= entries[i];
				DWORD	name_len	= max_subkey_len;
				DWORD	class_len	= max_class_len;
				auto status			= RegEnumKeyExW(h1, i, name_buffer.begin(), &name_len, nullptr, class_buffer.begin(), &class_len, &entry.last_write_time);
				entry.name			= string(name_buffer.begin(), name_len);
				entry.class_name	= string(class_buffer.begin(), class_len);
			}

			if (h1 != h)
				RegCloseKey(h1);
			return entries;
		},
		[promise](napi_status status, const auto &entries) {
			Node::array result;
			for (auto &entry : entries) {
				result.push(Node::object::make(
					"name",				entry.name,
					"class",			entry.class_name,
					"last_write_time",	entry.last_write_time
				));

			}
			promise.resolve(result);
		}
	);

	return promise;
}

auto ConnectRegistry(LPCWSTR machineName, HKEY h) {	
	HKEY result;
	auto status = RegConnectRegistryW(machineName, h, &result);
	return result;
}

auto ConnectRegistryAsync(string machineName, HKEY h) {	
	Node::Promise	promise;

	auto worker = Node::async_work("ConnectRegistry",
		[h, machineName]() {
			struct Result { HKEY hkey; LSTATUS status; };
			Result result;
			result.status = RegConnectRegistryW(machineName, h, &result.hkey);
			return result;
		},
		[promise](napi_status status, auto result) {
			if (result.status == ERROR_SUCCESS)
				promise.resolve(result.hkey);
			else
				promise.reject(result.status);
		}
	);

	return promise;
}

//-----------------------------------------------------------------------------
// values
//-----------------------------------------------------------------------------

auto DeleteValue(HKEY h, LPCWSTR subkey, LPCWSTR value) {
	return RegDeleteKeyValueW(h, subkey, value);
}

auto GetValue(HKEY h, LPCWSTR subkey, LPCWSTR value, DWORD flags, range<byte*> data) {
	DWORD	type;
	auto	size	= (DWORD)data.size();
	auto	status	= RegGetValueW(h, subkey, value, flags, &type, data.begin(), &size);

	return Node::object::make(
		"status",	status,
		"type", 	type,
		"size",		size
	);
}

range<uint8_t*> _GetValue(HKEY h, LPCWSTR subkey, LPCWSTR value, DWORD flags, range<uint8_t*> data) {
	DWORD size = (DWORD)data.size();
	auto status = RegGetValueW(h, subkey, value, flags, nullptr, data.begin(), &size);
	return data.slice(0, size); // Return the actual size of the data read
}

Node::value GetValue2(HKEY h, LPCWSTR subkey, LPCWSTR value, DWORD flags) {
	DWORD	type;
	DWORD	size	= 0;
	auto	status	= RegGetValueW(h, subkey, value, flags, &type, nullptr, &size);
	uint8_t smalldata[16];
	return ToNative(type, _GetValue(h, subkey, value, flags, size < 16 ? range<uint8_t*>(smalldata) : growing_block<uint8_t>(size)));
/*
	uint8_t* 	data = nullptr;
	auto	buffer = Node::TypedArray<uint8_t>(size, &data);
	status = RegGetValueW(h, subkey, value, flags, nullptr, data, &size);
	return buffer;
*/
}

auto SetValue(HKEY h, LPCWSTR subkey, LPCWSTR value, DWORD type, range<byte*> data) {
	return RegSetKeyValueW(h, subkey, value, type, data.begin(), data.size());
}
/*
template<typename T>auto _SetValue(HKEY h, LPCWSTR subkey, LPCWSTR value, DWORD type, T data) {
	return RegSetKeyValueW(h, subkey, value, type, (byte*)&data, sizeof(data));
}
auto SetValue2(HKEY h, LPCWSTR subkey, LPCWSTR value, Node::value data) {
	switch (data.type()) {
		case napi_undefined:
		case napi_null:
			return RegSetKeyValueW(h, subkey, value, REG_NONE, nullptr, 0);

		case napi_boolean:
			return _SetValue(h, subkey, value, REG_DWORD, Node::boolean(data) ? 1 : 0);

		case napi_number: {
			auto num = Node::number(data);
			if (num < double(UINT32_MAX))
				return _SetValue(h, subkey, value, REG_DWORD, (uint32_t)num);
			return _SetValue(h, subkey, value, REG_QWORD, (int64_t)num);
		}
		case napi_bigint: {
			auto num = Node::bigint(data);
			return _SetValue(h, subkey, value, REG_QWORD, (int64_t)num);
		}

		case napi_string: {
			auto str = Node::string(data);
			auto len = str.length() + 1;
			growing_block<char16_t> buffer(len);
			str.get_utf16(buffer.begin(), len);
			return RegSetKeyValueW(h, subkey, value, REG_SZ, (const byte*)buffer.begin(), len * sizeof(char16_t));
		}

		case napi_object:
			if (auto buffer = Node::ArrayBuffer::is(data)) {
				const auto buf = buffer.native();
				return RegSetKeyValueW(h, subkey, value, REG_BINARY, buf.begin(), buf.size());

			} else if (auto buffer = Node::TypedArray<uint8_t>::is(data)) {
				const auto buf = buffer.native();
				return RegSetKeyValueW(h, subkey, value, REG_BINARY, buf.begin(), buf.size());

			} else if (auto array = Node::array::is(data)) {
				auto len = array.length();
				growing_block<char16_t> buffer;
				for (const Node::value item : array) {
					if (auto str = Node::string::is(item)) {
						auto len	= str.length() + 1;
						str.get_utf16(buffer.alloc(len), len);
					}
				}
				
				*buffer.alloc(1) = 0; // Null-terminate the multi-string
				return RegSetKeyValueW(h, subkey, value, REG_MULTI_SZ, (const byte*)buffer.begin(), (buffer.p - buffer.begin()) * sizeof(char16_t));
			}
			break;

		default:
			break;
	}

	return (LSTATUS)-1;
}
*/
auto SetValue2(HKEY h, LPCWSTR subkey, LPCWSTR value, Node::value data) {
	auto v		= FromNative(data);
	auto buf	= v.data.native();
	return RegSetKeyValueW(h, subkey, value, v.type, buf.begin(), buf.size());
}


auto EnumValue(HKEY h, int index, range<uint16_t*> name_buffer, range<uint8_t*> data_buffer) {
	DWORD type;
	DWORD name_size = name_buffer.size();
	DWORD data_size = data_buffer.size();

	auto status = RegEnumValueW(h, index, (wchar_t*)name_buffer.begin(), &name_size, nullptr, &type, data_buffer.begin(), &data_size);

	return Node::object::make(
		"status",	status,
		"name",		Node::string((char16_t*)name_buffer.begin(), name_size),
		"type", 	type,
		"size",		data_size
	);
}

auto EnumValues(HKEY h, LPCWSTR subkey) {
	Node::array result;

	auto h0 = h;
	auto status = RegOpenKeyExW(h0, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE, &h);
	if (status != ERROR_SUCCESS)
		return result; // Return empty array if key cannot be opened

	DWORD values				= 0;
	DWORD max_value_name_len	= 0;
	DWORD max_value_len			= 0;
	status = RegQueryInfoKeyW(h,
		nullptr, nullptr,
		nullptr,
		nullptr, nullptr, nullptr,
		&values, &max_value_name_len, &max_value_len,
		nullptr, nullptr
	);

	++max_value_name_len; // +1 for null terminator
	auto name_buffer = (wchar_t*)malloc(max_value_name_len * sizeof(wchar_t));
	auto data_buffer = (uint8_t*)malloc(max_value_len);

	for (int i = 0; i < values; ++i) {
		DWORD name_len = max_value_name_len;
		DWORD data_len = max_value_len;
		DWORD type;
		auto status = RegEnumValueW(h, i, name_buffer, &name_len, nullptr, &type, data_buffer, &data_len);
		void *raw;
		auto data = Node::ArrayBuffer(data_len, &raw);
		memcpy(raw, data_buffer, data_len);
		result.push(Node::object::make(
			"name", Node::string((char16_t*)name_buffer, name_len),
			"type", type,
			"data", data
		));
	}

	free(name_buffer);
	free(data_buffer);
	if (h != h0)
		RegCloseKey(h);

	return result;
}
/*
void TestASync() {
	auto func = Node::function("callback", []() {
      	return Node::string("Async callback fired!");
    });
	Node::global->call_async(async_context, "setTimeout", func, 100);
}
*/
/*
Node::Promise EnumValuesAsync(HKEY h, string subkey) {
	Node::Promise promise;
	auto worker = Node::async_work("EnumValues",
		[]() {

		},
		[h, subkey, promise](napi_status status) {
			promise.resolve(EnumValues(h, subkey));
		}
	);
	return promise;
}
*/

Node::Promise EnumValuesAsync(HKEY h, string subkey) {
	Node::Promise promise;

	HKEY h1;
	auto status = RegOpenKeyExW(h, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE, &h1);
	if (status != ERROR_SUCCESS) {
		promise.reject(status);
		return promise;
	}

	DWORD values				= 0;
	DWORD max_value_name_len	= 0;
	DWORD max_value_len			= 0;

	status = RegQueryInfoKeyW(h1,
		nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr,
		&values, &max_value_name_len, &max_value_len,
		nullptr, nullptr
	);
	++max_value_name_len; // +1 for null terminator

	auto worker = Node::async_work("EnumValues",
		[h, h1, values, max_value_name_len, max_value_len]() {
			struct Entry {
				string	name;
				DWORD	type;
				DWORD	size;
			};
			struct Transfer {
				alloc_block<Entry>		entries;
				growing_block<uint8_t>  data;
			};
			Transfer transfer;
			transfer.entries = alloc_block<Entry>(values);

			alloc_block<wchar_t> name_buffer(max_value_name_len);

			for (int i = 0; i < values; i++) {
				auto &entry		= transfer.entries[i];
				DWORD name_len	= max_value_name_len;
				DWORD data_len	= max_value_len;

				auto status		= RegEnumValueW(h1, i, name_buffer.begin(), &name_len, nullptr, &entry.type, transfer.data.ensure(max_value_len), &data_len);
				transfer.data.alloc(data_len);

				entry.name		= string(name_buffer.begin(), name_len);
				entry.size		= data_len;
			}

			if (h1 != h)
				RegCloseKey(h1);

			return transfer;
		},
		[promise](napi_status status, const auto &transfer) {
			void*	raw;
			auto	data_total = transfer.data.tell();
			auto	array_buffer = Node::ArrayBuffer(data_total, &raw);
			memcpy(raw, transfer.data.begin(), data_total);

			Node::array result;
			size_t offset = 0;
			for (auto &entry : transfer.entries) {
                result.push(Node::object::make(
                    "name", entry.name,
                    "type", entry.type,
                    "data", Node::TypedArray<uint8_t>(array_buffer, offset, entry.size)
                ));
				offset += entry.size;
            }
			promise.resolve(result);
		}
	);

	return promise;
}

auto EnumValuesMulti(HKEY h, LPCWSTR subkey, Node::array values) {
	Node::array result;

	auto h0 = h;
	auto status = RegOpenKeyExW(h0, subkey, 0, KEY_WRITE | KEY_QUERY_VALUE, &h);
	if (status != ERROR_SUCCESS)
		return result; // Return empty array if key cannot be opened

	growing_block<VALENTW> val_list(values.length());
	for (auto i : values) {
		auto name	= Node::string((Node::value)i);
		auto len	= name.length() + 1; // +1 for null terminator
		auto buffer = (char16_t*)malloc(len * sizeof(char16_t));
		name.get_utf16(buffer, len);
		val_list[i.i].ve_valuename = (wchar_t*)buffer;
	}

	DWORD	total = 0;
	status = RegQueryMultipleValuesW(h, val_list.begin(), val_list.size(), nullptr, &total);

	if (status == ERROR_MORE_DATA) {
		void *raw;
		auto array_buffer = Node::ArrayBuffer(total, &raw);
		status = RegQueryMultipleValuesW(h, val_list.begin(), val_list.size(), (wchar_t*)raw, &total);

		for (auto& val : val_list) {
			result.push(Node::object::make(
				"name", Node::string((char16_t*)val.ve_valuename),
				"type", val.ve_type,
				"data", Node::TypedArray<uint8_t>(array_buffer, val.ve_valueptr - (DWORD_PTR)raw, val.ve_valuelen)
			));

			free((void*)val.ve_valuename); // Free the allocated name buffer
		}
	}

	if (h != h0)
		RegCloseKey(h);

	return result;
}

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------

extern "C" {
	NAPI_MODULE_EXPORT int32_t NODE_API_MODULE_GET_API_VERSION(void) { return NAPI_VERSION; }
	NAPI_MODULE_EXPORT napi_value NAPI_MODULE_INITIALIZER(napi_env env, napi_value exports) {
	Node::global_env	= env;

    Node::object    async_resource;
	napi_async_init(Node::global_env, async_resource, Node::string("my_async"), &async_context);


	Node::object(exports).defineProperties({
	{"CreateKey", Node::function::make<CreateKey>()},
	{"OpenKey", Node::function::make<OpenKey>()},
	{"CloseKey", Node::function::make<CloseKey>()},
	{"RenameKey", Node::function::make<RenameKey>()},
	{"DeleteKey", Node::function::make<DeleteKey>()},
	{"DeleteTree", Node::function::make<DeleteTree>()},
	{"DeleteTreeAsync", Node::function::make<DeleteTreeAsync>()},
	{"ConnectRegistry", Node::function::make<ConnectRegistry>()},
	{"ConnectRegistryAsync", Node::function::make<ConnectRegistryAsync>()},

	{"QueryKey", Node::function::make<QueryKey>()},
	{"EnumKey", Node::function::make<EnumKey>()},
	{"EnumKeys", Node::function::make<EnumKeys>()},
	{"EnumKeysAsync", Node::function::make<EnumKeysAsync>()},

	{"DeleteValue", Node::function::make<DeleteValue>()},
	{"GetValue", Node::function::make<GetValue>()},
	{"GetValue2", Node::function::make<GetValue2>()},
	{"SetValue", Node::function::make<SetValue>()},
	{"SetValue2", Node::function::make<SetValue2>()},

	{"EnumValue", Node::function::make<EnumValue>()},
	{"EnumValues", Node::function::make<EnumValues>()},
	{"EnumValuesAsync", Node::function::make<EnumValuesAsync>()},
	{"EnumValuesMulti", Node::function::make<EnumValuesMulti>()},

	{"ToNative", Node::function::make<ToNative>()},
	{"FromNative", Node::function::make<FromNative>()},


	//{"TestASync", Node::function::make<TestASync>()},

	});

	return exports;
	}
}
