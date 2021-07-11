#include <windows.h>
#include <dsound.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define pi32 3.14159265359f

// typedef HRESULT WINAPI dsound_create(LPGUID lpGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter);

struct win32_offscreen_buffer {
	BITMAPINFO info;
	void *memory;
	int width;
	int height;
	int bytes_per_pixel;
	int pitch;
};

struct window_dimension {
	int width;
	int height;
};

struct read_file_result {
	unsigned int contents_size;
	void *contents;
};

struct bitmap_result {
	int width;
	int height;
	int stride;
	int bytes_per_pixel;
	unsigned int *pixels;
};

struct character_bitmap_result {
	int width;
	int height;
	unsigned char *pixels;
};

struct game_assets {
	character_bitmap_result character_bitmaps[512];
};

struct keyboard_input {
	int digit_pressed;
	unsigned char backspace_pressed;	// BOOL
	unsigned char return_pressed;		// BOOL
};

struct coordinate {
	int x;
	int y;
};

// GLOBALS
bool should_quit = false;
LPDIRECTSOUNDBUFFER secondary_buffer;
win32_offscreen_buffer global_buffer;
game_assets global_assets;
keyboard_input k_input;

unsigned int charset_size = 62;
char *charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

HDC device_context;

void free_file_memory(void *memory) {
	if (memory) {
		VirtualFree(memory, 0, MEM_RELEASE);
	}
}

read_file_result read_entire_file(LPCSTR filename) {
	read_file_result result = {};

	HANDLE file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

	if (file_handle != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER file_size;
		if(GetFileSizeEx(file_handle, &file_size)) {
			unsigned int file_size32 = (unsigned int)file_size.QuadPart;	// NOTE: Probably unsafe truncation here
			result.contents = VirtualAlloc(0, file_size32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

			if (result.contents) {
				DWORD bytes_read;
				if (ReadFile(file_handle, result.contents, file_size32, &bytes_read, 0) && (file_size32 == bytes_read)) {
					// File read successfully.
					result.contents_size = file_size32;
				} else {
					// TODO: Logging
					free_file_memory(result.contents);
					result.contents = 0;
				}
			} else {
				// TODO: Logging
			}
		} else {
			// TODO: Logging
		}

		CloseHandle(file_handle);
	} else {
		// TODO: Logging
	}

	return result;
}

window_dimension get_window_dimension(HWND window) {
	RECT client_rect;
	GetClientRect(window, &client_rect);

	window_dimension result;

	result.width = client_rect.right - client_rect.left;
	result.height = client_rect.bottom - client_rect.top;

	return result;
}

void resize_dib_section(win32_offscreen_buffer* buffer, int width, int height) {
	if (buffer->memory) {
		VirtualFree(buffer->memory, 0, MEM_RELEASE);
	}

	int bytes_per_pixel = 4;

	buffer->width = width;
	buffer->height = height;

	buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
	buffer->info.bmiHeader.biWidth = buffer->width;
	buffer->info.bmiHeader.biHeight = -buffer->height;
	buffer->info.bmiHeader.biPlanes = 1;
	buffer->info.bmiHeader.biBitCount = 32;
	buffer->info.bmiHeader.biCompression = BI_RGB;

	int bitmap_memory_size = (buffer->width * buffer->height) * bytes_per_pixel;
	buffer->memory = VirtualAlloc(0, bitmap_memory_size, MEM_COMMIT, PAGE_READWRITE);

	buffer->pitch = buffer->width * bytes_per_pixel;
	buffer->bytes_per_pixel = bytes_per_pixel;
}

void display_buffer_in_window(HDC device_context, window_dimension dimension) {
	StretchDIBits(device_context,
				  0, 0, dimension.width, dimension.height,
				  0, 0, global_buffer.width, global_buffer.height,
				  global_buffer.memory,
				  &global_buffer.info,
				  DIB_RGB_COLORS, SRCCOPY);
}

bitmap_result debug_load_bitmap(char* filename) {
	bitmap_result bmp_result = {};

	read_file_result file_result = read_entire_file(filename);
	unsigned char *contents = (unsigned char *)file_result.contents;

	BITMAPFILEHEADER *file_header = (BITMAPFILEHEADER *)contents;
	BITMAPINFOHEADER *info_header = (BITMAPINFOHEADER *)(contents + 14);  // We are assuming file header takes 14 bytes.

	bmp_result.width = info_header->biWidth;
	bmp_result.height = info_header->biHeight;
	bmp_result.stride = ((((info_header->biWidth * info_header->biBitCount) + 31) & ~31) >> 3);
	bmp_result.bytes_per_pixel = info_header->biBitCount / 8;
	bmp_result.pixels = (unsigned int *)(contents + file_header->bfOffBits);

	return bmp_result;
}

void debug_paint_window(unsigned int color) {
	unsigned int *pixel = (unsigned int *)global_buffer.memory;

	for (int i = 0; i < global_buffer.height; i++) {
		for (int j = 0; j < global_buffer.width; j++) {
			*pixel++ = color;
		}
	}
}

void render_bitmap(int x_pos, int y_pos, win32_offscreen_buffer *buffer, bitmap_result bmp) {
	unsigned int *dest_row = (unsigned int *)buffer->memory;
	dest_row += y_pos * (buffer->pitch / 4) + x_pos;

	// NOTE: Doing this calculation on the source row because the bitmaps are bottom up,
	// whereas the window is top-down. So must start at the bottom of the source bitmap,
	// working left to right.
	unsigned int *source_row = bmp.pixels + ((bmp.stride / 4) * (bmp.height - 1));

	for (int y = y_pos; y < y_pos + bmp.height; y++) {
		unsigned char *dest = (unsigned char *)dest_row;
		unsigned char *source = (unsigned char *)source_row;

		for (int x = x_pos; x < x_pos + bmp.width; x++) {
			for (int i = 0; i < 3; i++) {
				*dest = *source;
				dest++;
				source++;
			}

			*dest = 0xFF;
			dest++;
		}

		dest_row += buffer->pitch / 4;
		source_row -= bmp.stride / 4;
	}
}

void render_character_bitmap(int x_pos, int y_pos, win32_offscreen_buffer *buffer, character_bitmap_result bmp) {
	unsigned int *dest_row = (unsigned int *)buffer->memory;
	dest_row += y_pos * (buffer->pitch / 4) + x_pos;

	unsigned char *source_row = bmp.pixels;

	for (int y = y_pos; y < y_pos + bmp.height; y++) {
		unsigned int *dest = dest_row;
		unsigned char *source = (unsigned char *)source_row;

		for (int x = x_pos; x < x_pos + bmp.width; x++) {
			unsigned char alpha = *source;
			*dest = (alpha << 24) | (alpha << 16) | (alpha << 8) | (alpha << 0);
			dest++;
			source++;
		}

		dest_row += buffer->pitch / 4;
		source_row += bmp.width;
	}
}

void debug_render_string(int starting_x_pos, int y_pos, win32_offscreen_buffer *buffer, char *str) {
	int x = starting_x_pos;
	for (int i = 0; i < strlen(str); i++) {
		render_character_bitmap(x, y_pos, buffer, global_assets.character_bitmaps[str[i]]);
		x += global_assets.character_bitmaps[str[i]].width;
	}
}

void init_dsound(HWND window, int samples_per_second, int buffer_size) {
	LPDIRECTSOUND direct_sound;
	if (SUCCEEDED(DirectSoundCreate(0, &direct_sound, 0))) {
		WAVEFORMATEX wave_format = {};
		wave_format.wFormatTag = WAVE_FORMAT_PCM;
		wave_format.nChannels = 2;
		wave_format.nSamplesPerSec = samples_per_second;
		wave_format.wBitsPerSample = 16;
		wave_format.nBlockAlign = (wave_format.nChannels * wave_format.wBitsPerSample) / 8;
		wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
		wave_format.cbSize = 0;

		if (SUCCEEDED(direct_sound->SetCooperativeLevel(window, DSSCL_PRIORITY))) {
			DSBUFFERDESC primary_buffer_desc = {};
			primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
			primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;

			LPDIRECTSOUNDBUFFER primary_buffer;
			if (SUCCEEDED(direct_sound->CreateSoundBuffer(&primary_buffer_desc, &primary_buffer, 0))) {
				if (SUCCEEDED(primary_buffer->SetFormat(&wave_format))) {
					OutputDebugStringA("Primary buffer format was set.\n");
				} else {
					// TODO: Diagnostic
				}
			} else {
				// TODO: Diagnostic
			}

			DSBUFFERDESC secondary_buffer_desc = {};
			secondary_buffer_desc.dwSize = sizeof(secondary_buffer_desc);
			secondary_buffer_desc.dwFlags = 0;
			secondary_buffer_desc.dwBufferBytes = buffer_size;
			secondary_buffer_desc.lpwfxFormat = &wave_format;

			if (SUCCEEDED(direct_sound->CreateSoundBuffer(&secondary_buffer_desc, &secondary_buffer, 0))) {
				OutputDebugStringA("Secondary buffer was created.\n");
			} else {
				// TODO: Diagnostic
			}
		} else {
			// TODO: Diagnostic
		}
	} else {
		// TODO: Diagnostic
	}
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
	LRESULT result = 0;

	switch (message) {
		break;
		case WM_SIZE: {
			// window_dimension dim = get_window_dimension(window);
			// resize_dib_section(&global_buffer, dim.width, dim.height);
		}
		break;

		case WM_CLOSE: {
			OutputDebugStringA("WM_CLOSE\n");
			should_quit = true;
		}
		break;

		case WM_ACTIVATEAPP: {
			OutputDebugStringA("WM_ACTIVATEAPP\n");
		}
		break;

		case WM_DESTROY: {
			OutputDebugStringA("WM_DESTROY\n");
		}
		break;

		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC device_context = BeginPaint(window, &paint);

			window_dimension dimension = get_window_dimension(window);
			display_buffer_in_window(device_context, dimension);

			OutputDebugStringA("WM_PAINT\n");

			EndPaint(window, &paint);
		}
		break;

		case WM_SETFONT: {
			OutputDebugStringA("WM_SETFONT\n");
		}
		break;

		case WM_KEYDOWN: {
			OutputDebugStringA("KEY DOWN\n");

			if (w_param == VK_RETURN) {
				k_input.return_pressed = 1;;
			}

			if (w_param >= 0x30 && w_param <= 0x39) {
				k_input.digit_pressed = w_param % 0x30;	// This will give us just the digit
			}

			if (w_param >= 0x60 && w_param <= 0x69) {	// Number pad
				k_input.digit_pressed = w_param % 0x60;
			}

			if (w_param == VK_BACK) {
				k_input.backspace_pressed = 1;
			}
		}
		break;

		default: {
			result = DefWindowProc(window, message, w_param, l_param);
		}
		break;
	}

	return result;
}

#include "main.cpp"

struct win32_sound_output {
		int samples_per_second;
		int hz;
		int tone_volume;
		int wave_period;
		int bytes_per_sample;
		unsigned int running_sample_index;
		int secondary_buffer_size;
};

void fill_sound_buffer(win32_sound_output *sound_output, DWORD byte_to_lock, DWORD bytes_to_write) {
	VOID *region1;
	DWORD region1_size;
	VOID *region2;
	DWORD region2_size;
	if (SUCCEEDED(secondary_buffer->Lock(
		byte_to_lock,
		bytes_to_write,
		&region1, &region1_size,
		&region2, &region2_size,
		0
	))) {
		DWORD region1_sample_count = region1_size / sound_output->bytes_per_sample;
		short *sample_out = (short *)region1;
		for (int sample_index = 0; sample_index < region1_sample_count; sample_index++) {
			float t = 2.0f * pi32 * (float)sound_output->running_sample_index / (float)sound_output->wave_period;
			float sine_value = sinf(t);

			short sample_value = (short)(sine_value * sound_output->tone_volume);
			*sample_out++ = sample_value;
			*sample_out++ = sample_value;
			sound_output->running_sample_index++;
		}

		DWORD region2_sample_count = region2_size / sound_output->bytes_per_sample;
		sample_out = (short *)region2;
		for (int sample_index = 0; sample_index < region2_sample_count; sample_index++) {
			float t = 2.0f * pi32 * (float)sound_output->running_sample_index / (float)sound_output->wave_period;
			float sine_value = sinf(t);

			short sample_value = (short)(sine_value * sound_output->tone_volume);
			*sample_out++ = sample_value;
			*sample_out++ = sample_value;
			sound_output->running_sample_index++;
		}

		secondary_buffer->Unlock(
			region1, region1_size,
			region2, region2_size
		);
	}
}

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR command_line, int show_code) {
	WNDCLASS window_class = {};

	window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.lpszClassName = "MainWindowClass";

	if (RegisterClassA(&window_class)) {
		HWND window_handle = CreateWindowExA(0, window_class.lpszClassName, "Main",
			                          WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
									  CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, instance, 0);

		if (window_handle) {
			device_context = GetDC(window_handle);

			window_dimension dim = get_window_dimension(window_handle);
			resize_dib_section(&global_buffer, dim.width, dim.height);

			win32_sound_output sound_output = {};
			sound_output.samples_per_second = 48000;
			sound_output.hz = 440;
			sound_output.tone_volume = 6000;
			sound_output.wave_period = sound_output.samples_per_second / sound_output.hz;
			sound_output.bytes_per_sample = sizeof(short) * 2;
			sound_output.running_sample_index = 0;
			sound_output.secondary_buffer_size = sound_output.samples_per_second * sound_output.bytes_per_sample;  // 1 second buffer

			init_dsound(window_handle, sound_output.samples_per_second, sound_output.secondary_buffer_size);
			fill_sound_buffer(&sound_output, 0, sound_output.secondary_buffer_size);
			secondary_buffer->Play(0, 0, DSBPLAY_LOOPING);

			// LOAD ASSETS

			stbtt_fontinfo font_info;

			read_file_result ttf_file;
			ttf_file = read_entire_file("c:/windows/fonts/arial.ttf");

			stbtt_InitFont(&font_info, (unsigned char *)ttf_file.contents, stbtt_GetFontOffsetForIndex((unsigned char *)ttf_file.contents, 0));

			for (int i = 0; i < charset_size; i++) {
				int font_width;
				int font_height;
				unsigned char *char_bitmap = stbtt_GetCodepointBitmap(&font_info, 0, stbtt_ScaleForPixelHeight(&font_info, 128.0f), charset[i], &font_width, &font_height, 0, 0);

				global_assets.character_bitmaps[charset[i]].width = font_width;
				global_assets.character_bitmaps[charset[i]].height = font_height;
				global_assets.character_bitmaps[charset[i]].pixels = char_bitmap;
			}
/*
			int asset_index = 0;
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 13; j++) {
					char path[50];
					sprintf(path, "c:/projects/native_poker/card-BMPs/%c%s.bmp", suit_chars[i], value_strings[j]);
					bitmap_result bmp = debug_load_bitmap(path);

					card_image c_image = {};
					c_image.bmp = bmp;
					sprintf(c_image.id, "%c%s", suit_chars[i], value_strings[j]);

					global_assets.card_images[asset_index] = c_image;
					asset_index++;
				}
			}

			global_assets.face_down_card_image = debug_load_bitmap("c:/projects/native_poker/card-BMPs/b1fv.bmp");
*/
			// MESSAGE LOOP
			while (!should_quit) {
				k_input = {};
				k_input.digit_pressed = -1;	// This is the default value. We can't use 0 since that is a valid value here

				MSG msg;

				while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
					if (msg.message == WM_QUIT) {
						should_quit = true;
					}

					TranslateMessage(&msg);
					DispatchMessageA(&msg);
				}

				debug_paint_window(0x434591);
				// debug_render_bitmap(bmp);

				DWORD play_cursor;
				DWORD write_cursor;
				if (SUCCEEDED(secondary_buffer->GetCurrentPosition(&play_cursor, &write_cursor))) {
					DWORD byte_to_lock = (sound_output.running_sample_index * sound_output.bytes_per_sample) % sound_output.secondary_buffer_size;
					DWORD bytes_to_write;

					// TODO: Change this to use lower latency offset from the play cursor.
					if (byte_to_lock == play_cursor) {
						bytes_to_write = 0;
					} else if (byte_to_lock > play_cursor) {
						bytes_to_write = sound_output.secondary_buffer_size - byte_to_lock + play_cursor;
					} else {
						bytes_to_write = play_cursor - byte_to_lock;
					}

					fill_sound_buffer(&sound_output, byte_to_lock, bytes_to_write);
				}

				update_and_render(&global_buffer, &global_assets, &k_input);

				window_dimension dimension = get_window_dimension(window_handle);
				display_buffer_in_window(device_context, dimension);
			}
		} else {
			OutputDebugStringA("ERROR: Unable to create window.");
		}
	} else {
		OutputDebugStringA("ERROR: Unable to register the window class.");
	}
}
