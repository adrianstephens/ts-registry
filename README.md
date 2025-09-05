# @isopodlabs/registry
[![npm version](https://img.shields.io/npm/v/@isopodlabs/registry.svg)](https://www.npmjs.com/package/@isopodlabs/registry)
[![GitHub stars](https://img.shields.io/github/stars/adrianstephens/ts-registry.svg?style=social)](https://github.com/adrianstephens/ts-registry)
[![License](https://img.shields.io/npm/l/@isopodlabs/registry.svg)](LICENSE.txt)

A TypeScript library for accessing and manipulating the Windows Registry with both command-line and native API support.

## ☕ Support My Work
If you use this package, consider [buying me a cup of tea](https://coff.ee/adrianstephens) to support future updates!

## Installation

```sh
npm install @isopodlabs/registry
```

## Features

### Registry Access
- **Read/Write operations** - Get and set registry values with type safety
- **Key management** - Create, delete, and enumerate registry keys
- **Multiple data types** - Support for all Windows registry data types
- **Remote registry** - Access registry on remote machines
- **32/64-bit views** - Handle WOW64 registry redirection

### Data Types
- **REG_SZ** - String values
- **REG_EXPAND_SZ** - Expandable string values
- **REG_DWORD** - 32-bit integers
- **REG_QWORD** - 64-bit integers
- **REG_BINARY** - Binary data
- **REG_MULTI_SZ** - Multi-string arrays
- **REG_NONE** - No data type

### Advanced Features
- **Search functionality** - Find keys and values with pattern matching
- **Export/Import** - Registry file operations
- **Async operations** - Promise-based API
- **Type conversion** - Automatic data type handling
- **Custom reg.exe** - Use alternative registry executable for enhanced functionality

## Usage

### Basic Operations

```typescript
import { HKLM, HKCU, SZ, DWORD } from '@isopodlabs/registry';

// Read a registry value
const key = await HKLM.subkey('SOFTWARE\\Microsoft\\Windows\\CurrentVersion');
const productName = key.values.ProductName;
console.log('Windows version:', productName);

// Write a registry value
await key.setValue('MyValue', new SZ('Hello World'));

// Create a new key
const myKey = await HKLM.subkey('SOFTWARE\\MyApp').create();
await myKey.setValue('Version', new DWORD(100));
```

### Working with Different Hives

```typescript
import { 
    HKEY_LOCAL_MACHINE, 
    HKEY_CURRENT_USER,
    HKEY_CLASSES_ROOT,
    HKEY_USERS,
    HKEY_CURRENT_CONFIG 
} from '@isopodlabs/registry';

// Or use short names
import { HKLM, HKCU, HKCR, HKU, HKCC } from '@isopodlabs/registry';

// Access different registry hives
const systemKey = await HKLM.subkey('SYSTEM\\CurrentControlSet');
const userKey = await HKCU.subkey('Software\\Microsoft');
```

### Data Types

```typescript
import { SZ, DWORD, QWORD, BINARY, MULTI_SZ } from '@isopodlabs/registry';

// String value
await key.setValue('StringValue', new SZ('Hello'));

// Number values
await key.setValue('NumberValue', new DWORD(42));
await key.setValue('BigNumber', new QWORD(BigInt(123456789)));

// Binary data
await key.setValue('BinaryValue', new BINARY(new Uint8Array([1, 2, 3, 4])));

// Multiple strings
await key.setValue('MultiString', new MULTI_SZ(['item1', 'item2', 'item3']));
```

### Key Management

```typescript
// Check if key exists
if (await key.exists()) {
    console.log('Key exists');
}

// Create a new key
const newKey = await HKLM.subkey('SOFTWARE\\MyApp').create();

// Delete a key
await key.destroy();

// Delete a specific value
await key.deleteValue('MyValue');

// Enumerate subkeys
for (const subkey of key) {
    console.log('Subkey:', subkey.name);
}
```

### Search Operations

```typescript
// Search for keys and values
const results = [];
const searchPromise = key.search('MyPattern', {
    found: (line) => results.push(line)
}, {
    recursive: true,
    case_sensitive: false,
    keys: true,
    values: true,
    data: true
});

await searchPromise;
console.log('Search results:', results);
```

### Remote Registry Access

```typescript
import { host } from '@isopodlabs/registry';

// Access remote machine registry
const remoteHost = host('REMOTE-PC');
const remoteKey = await remoteHost.HKLM.subkey('SOFTWARE\\Microsoft');
```

### 32/64-bit Registry Views

```typescript
import { view32, view64 } from '@isopodlabs/registry';

// Access 32-bit registry view
const key32 = await view32.HKLM.subkey('SOFTWARE\\MyApp');

// Access 64-bit registry view (default)
const key64 = await view64.HKLM.subkey('SOFTWARE\\MyApp');
```

### Export/Import Operations

```typescript
// Export registry key to file
await key.export('backup.reg');

// Import registry file
import { importReg } from '@isopodlabs/registry';
await importReg('backup.reg');
```

## API Reference

### Core Classes
- `KeyPromise` - Base registry key class with async operations
- `Key` - Registry key with synchronous property access
- `KeyHost` - Registry host for remote access
- `View` - Registry view for 32/64-bit access

### Data Type Classes
- `SZ` - String data type
- `EXPAND_SZ` - Expandable string data type
- `DWORD` - 32-bit integer data type
- `QWORD` - 64-bit integer data type
- `BINARY` - Binary data type
- `MULTI_SZ` - Multi-string data type
- `NONE` - No data type

### Utility Functions
- `getKey(path, view?)` - Get registry key by path
- `importReg(file, machine?, view?)` - Import registry file
- `setExecutable(path?)` - Set custom reg.exe path
- `reset(view?, dirty?)` - Reset cached registry data

## Error Handling

```typescript
try {
    const key = await HKLM.subkey('NonExistent\\Key');
    const value = key.values.NonExistentValue;
} catch (error) {
    console.error('Registry operation failed:', error.message);
}
```

## Custom Registry Executable

The library includes a custom C++ implementation of `reg.exe` that provides enhanced functionality and better compatibility. You can use it by calling:

```typescript
import { setExecutable } from '@isopodlabs/registry';

// Use custom reg.exe implementation
await setExecutable('./path/to/custom-reg.exe');

// Reset to system default
await setExecutable();
```

The custom implementation offers:
- **Better Unicode support** - Handles UTF-8 and UTF-16 registry files
- **Enhanced error reporting** - More detailed error messages
- **Improved performance** - Optimized for programmatic use
- **Extended functionality** - Additional features not in standard reg.exe

## Platform Support

This library is designed for Windows systems and requires:
- Windows operating system
- `reg.exe` command-line tool (included with Windows) or custom implementation
- Node.js runtime

## License

This project is licensed under the MIT License.