// Aturan nama dan penyisipan FX dari spesifikasi (bagian 5.2). Tanpa AudioToolbox, supaya bisa diuji offline.
// Hanya tipe Audio Unit yang dihosting backend saat ini. Prefix lain dikenali, tetapi ditolak.
#ifndef AUDIOHUB_FX_RULES_H
#define AUDIOHUB_FX_RULES_H

#include <string>
#include <vector>

namespace audiohub {
namespace fx {

enum class Kind { Au, Vst3, Vst2, Vst, Js, Dx };

struct ParsedName {
    Kind kind = Kind::Au;     // tanpa prefix dianggap Audio Unit
    std::string name;         // nama tanpa prefix
    bool fxAdd = false;       // prefix FXADD: (pilihan dari browser FX), belum didukung
};

// Memisahkan prefix tipe dari nama: "VST3:Reverb" → Vst3 "Reverb". Prefix yang tidak dikenal dibiarkan utuh.
ParsedName parseName(const std::string& raw);

// true jika tipe ini dapat dihosting backend saat ini.
bool isSupported(Kind kind);

// Mengembalikan indeks FX dalam chain setelah aturan diterapkan, atau -1 jika gagal.
// chain: nama FX berurutan (indeks 0 = pertama). Pencocokan memakai nama tanpa prefix.
//   instantiate < 0 (−1 … −999): selalu tambah di akhir.
//   instantiate = 0: kembalikan indeks instance pertama, atau -1 jika tidak ada.
//   instantiate > 0: kembalikan indeks instance pertama jika ada; jika tidak, tambah di akhir.
//   instantiate ≤ −1000: sisipkan di posisi (−1000 − instantiate); −1000 = pertama, −1001 = kedua.
//     Posisi di atas jumlah FX gagal (−1). Ini pilihan implementasi: spesifikasi tidak menyebut batasnya.
int resolveInsert(const std::vector<std::string>& chain, const ParsedName& name, int instantiate);

}  // namespace fx
}  // namespace audiohub

#endif
