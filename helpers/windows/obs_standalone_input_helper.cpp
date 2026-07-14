#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

bool pointerDown = false;
bool pointerMoved = false;

std::string utf8(const std::wstring &value)
{
	if (value.empty())
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
					     nullptr, nullptr);
	std::string output(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr,
			    nullptr);
	return output;
}

std::string escapeJson(const std::string &value)
{
	std::string output;
	output.reserve(value.size() + 8);
	for (const unsigned char character : value) {
		if (character == '\\' || character == '"') {
			output.push_back('\\');
			output.push_back(static_cast<char>(character));
		} else if (character >= 0x20) {
			output.push_back(static_cast<char>(character));
		}
	}
	return output;
}

void emitLine(const std::string &line)
{
	fwrite(line.data(), 1, line.size(), stdout);
	fwrite("\n", 1, 1, stdout);
	fflush(stdout);
}

std::wstring processPath(DWORD processId)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process)
		return {};
	std::wstring path(32768, L'\0');
	DWORD size = static_cast<DWORD>(path.size());
	if (!QueryFullProcessImageNameW(process, 0, path.data(), &size))
		size = 0;
	CloseHandle(process);
	path.resize(size);
	return path;
}

std::wstring baseName(const std::wstring &path)
{
	const size_t separator = path.find_last_of(L"\\/");
	return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

std::string foregroundIdentity()
{
	HWND foreground = GetForegroundWindow();
	DWORD processId = 0;
	GetWindowThreadProcessId(foreground, &processId);
	const std::string process = escapeJson(utf8(baseName(processPath(processId))));
	return "\"app\":\"" + process + "\",\"process\":\"" + process + "\",\"bundle\":\"\"";
}

void emitInput(const char *kind)
{
	emitLine("{\"type\":\"input\",\"kind\":\"" + std::string(kind) + "\"," + foregroundIdentity() + "}");
}

void emitApplications()
{
	struct Item {
		std::wstring name;
		std::wstring process;
		std::wstring path;
		bool running = false;
	};
	std::map<std::wstring, Item> applications;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry)) {
			do {
				const std::wstring path = processPath(entry.th32ProcessID);
				const std::wstring key = path.empty() ? std::wstring(entry.szExeFile) : path;
				applications[key] = {entry.szExeFile, entry.szExeFile, path, true};
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
	}

	const auto readRegistryString = [](HKEY key, const wchar_t *name) {
		DWORD type = 0;
		DWORD size = 0;
		if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
		    (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t))
			return std::wstring();
		std::wstring value(size / sizeof(wchar_t), L'\0');
		if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size) !=
		    ERROR_SUCCESS)
			return std::wstring();
		while (!value.empty() && value.back() == L'\0')
			value.pop_back();
		if (type == REG_EXPAND_SZ) {
			std::wstring expanded(32768, L'\0');
			const DWORD length = ExpandEnvironmentStringsW(value.c_str(), expanded.data(),
								       static_cast<DWORD>(expanded.size()));
			if (length > 0 && length < expanded.size()) {
				expanded.resize(length - 1);
				value = expanded;
			}
		}
		return value;
	};

	const auto enumerateAppPaths = [&](HKEY root, REGSAM view) {
		HKEY parent = nullptr;
		if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths", 0, KEY_READ | view,
				  &parent) != ERROR_SUCCESS)
			return;
		for (DWORD index = 0;; ++index) {
			wchar_t name[512] = {};
			DWORD nameSize = 512;
			if (RegEnumKeyExW(parent, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr) !=
			    ERROR_SUCCESS)
				break;
			HKEY appKey = nullptr;
			if (RegOpenKeyExW(parent, name, 0, KEY_READ | view, &appKey) != ERROR_SUCCESS)
				continue;
			const std::wstring path = readRegistryString(appKey, nullptr);
			RegCloseKey(appKey);
			if (!path.empty() && applications.find(path) == applications.end())
				applications[path] = {baseName(path), name, path, false};
		}
		RegCloseKey(parent);
	};
	enumerateAppPaths(HKEY_CURRENT_USER, 0);
	enumerateAppPaths(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
	enumerateAppPaths(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);

	std::string items;
	bool first = true;
	for (const auto &[key, item] : applications) {
		const std::string name = escapeJson(utf8(item.name));
		const std::string process = escapeJson(utf8(item.process));
		const std::string path = escapeJson(utf8(item.path));
		if (name.empty() && process.empty())
			continue;
		if (!first)
			items += ',';
		first = false;
		items += "{\"name\":\"" + name + "\",\"process\":\"" + process + "\",\"bundle\":\"\",\"path\":\"" +
			 path + "\",\"running\":" + (item.running ? "true" : "false") + "}";
	}
	emitLine("{\"type\":\"apps\",\"items\":[" + items + "]}");
}

bool isTextKey(USHORT virtualKey)
{
	if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
	    (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
		return false;
	return (virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z') ||
	       virtualKey == VK_SPACE || (virtualKey >= VK_OEM_1 && virtualKey <= VK_OEM_8);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_INPUT) {
		UINT size = 0;
		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
		std::vector<BYTE> buffer(size);
		if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
				    sizeof(RAWINPUTHEADER)) != size)
			return 0;
		const RAWINPUT *input = reinterpret_cast<const RAWINPUT *>(buffer.data());
		if (input->header.dwType == RIM_TYPEKEYBOARD && !(input->data.keyboard.Flags & RI_KEY_BREAK)) {
			emitInput(isTextKey(input->data.keyboard.VKey) ? "text" : "keyboard");
		} else if (input->header.dwType == RIM_TYPEMOUSE) {
			const RAWMOUSE &mouse = input->data.mouse;
			if (pointerDown && (mouse.lLastX != 0 || mouse.lLastY != 0))
				pointerMoved = true;
			if (mouse.usButtonFlags &
			    (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN)) {
				pointerDown = true;
				pointerMoved = false;
			}
			if (mouse.usButtonFlags &
			    (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP)) {
				if (pointerDown)
					emitInput(pointerMoved ? "drag" : "click");
				pointerDown = false;
				pointerMoved = false;
			}
			if (mouse.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL))
				emitInput("wheel");
		}
		return 0;
	}
	if (message == WM_TIMER) {
		emitApplications();
		return 0;
	}
	if (message == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	SetConsoleOutputCP(CP_UTF8);
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = windowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = L"DrawStatsObsInputHelper";
	if (!RegisterClassW(&windowClass))
		return 1;
	HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance,
				      nullptr);
	if (!window)
		return 1;

	RAWINPUTDEVICE devices[2] = {
		{0x01, 0x06, RIDEV_INPUTSINK, window},
		{0x01, 0x02, RIDEV_INPUTSINK, window},
	};
	if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
		emitLine("{\"type\":\"permission\",\"state\":\"unavailable\"}");
		return 1;
	}
	emitLine("{\"type\":\"permission\",\"state\":\"allowed\"}");
	emitApplications();
	SetTimer(window, 1, 5000, nullptr);

	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return 0;
}
