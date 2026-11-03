// Penemuan endpoint audio, format campuran, daftar proses dengan sesi audio, dan tangkap audio per proses
// (WASAPI process loopback, Windows 10 build 19041 ke atas).
#ifndef AUDIOHUB_DEVICES_H
#define AUDIOHUB_DEVICES_H

// windows.h harus lebih dulu: header COM audio membutuhkan tipe-tipenya. NOMINMAX mencegah macro min/max
// bentrok dengan std::min dan std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace ahwin {

using Microsoft::WRL::ComPtr;

std::wstring toWide(const char* utf8);
std::string toUtf8(const std::wstring& wide);

// Endpoint aktif pertama yang nama friendly-nya mengandung `contains` (tidak peka huruf besar).
// flow: eCapture (input) atau eRender (output). nameOut menerima nama yang ditemukan.
ComPtr<IMMDevice> findEndpoint(EDataFlow flow, const std::wstring& contains, std::wstring* nameOut);

// Jumlah kanal dan sample rate format campuran (mix format) endpoint.
bool mixFormat(IMMDevice* dev, int* channels, uint32_t* sampleRate);

// Mengaktifkan process loopback untuk pid. Hasilnya IAudioClient yang belum diinisialisasi.
// Mengembalikan null jika gagal; err berisi pesan singkat.
ComPtr<IAudioClient> activateProcessLoopback(uint32_t pid, std::string& err);

// Membisukan atau membuka bisu sesi audio proses pid di endpoint render default.
// prevMute menerima status mute sesi pertama sebelum diubah. Mengembalikan jumlah sesi yang diubah.
int setSessionMute(uint32_t pid, bool mute, bool* prevMute);

// Teks daftar endpoint dan proses (format ada di audiohub_io.h).
std::string listEndpointsText();
std::string listProcessesText();

}  // namespace ahwin

#endif
