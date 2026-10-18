// Uji aturan nama dan penyisipan FX (prefix, instantiate, posisi sisip).
#include "../../engine/fx/fx_rules.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace audiohub::fx;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    // Prefix
    CHECK(parseName("Reverb").kind == Kind::Au);
    CHECK(parseName("Reverb").name == "Reverb");
    CHECK(parseName("AU:Reverb").kind == Kind::Au && parseName("AU:Reverb").name == "Reverb");
    CHECK(parseName("VST3:Plug").kind == Kind::Vst3 && parseName("VST3:Plug").name == "Plug");
    CHECK(parseName("VST2:Plug").kind == Kind::Vst2);
    CHECK(parseName("VST:Plug").kind == Kind::Vst);
    CHECK(parseName("JS:utility/gain").kind == Kind::Js);
    CHECK(parseName("DX:Echo").kind == Kind::Dx);
    CHECK(parseName("FXADD:2").fxAdd == true && parseName("FXADD:2").name == "2");
    // Prefix yang tidak dikenal dibiarkan utuh sebagai nama AU
    CHECK(parseName("Foo: Bar").kind == Kind::Au && parseName("Foo: Bar").name == "Foo: Bar");

    CHECK(isSupported(Kind::Au));
    CHECK(!isSupported(Kind::Vst3));

    // Chain contoh: [EQ, Comp, Reverb]
    std::vector<std::string> chain = {"EQ", "Comp", "Reverb"};
    ParsedName eq = parseName("EQ");
    ParsedName delay = parseName("Delay");

    // instantiate = 0: cari, tanpa tambah
    CHECK(resolveInsert(chain, parseName("Comp"), 0) == 1);
    CHECK(resolveInsert(chain, delay, 0) == -1);

    // instantiate > 0: cari, atau tambah di akhir jika belum ada
    CHECK(resolveInsert(chain, eq, 1) == 0);
    CHECK(resolveInsert(chain, delay, 1) == 3);

    // instantiate < 0 (−1…−999): selalu baru di akhir, meski sudah ada
    CHECK(resolveInsert(chain, eq, -1) == 3);
    CHECK(resolveInsert(chain, eq, -999) == 3);

    // instantiate ≤ −1000: posisi sisip. −1000 = pertama, −1001 = kedua
    CHECK(resolveInsert(chain, delay, -1000) == 0);
    CHECK(resolveInsert(chain, delay, -1001) == 1);
    CHECK(resolveInsert(chain, delay, -1003) == 3);   // tepat di akhir
    CHECK(resolveInsert(chain, delay, -1004) == -1);  // di luar rentang

    // Chain kosong
    std::vector<std::string> empty;
    CHECK(resolveInsert(empty, delay, 1) == 0);
    CHECK(resolveInsert(empty, delay, -1000) == 0);
    CHECK(resolveInsert(empty, delay, -1001) == -1);

    // Ditolak: tipe belum didukung, FXADD, nama kosong
    CHECK(resolveInsert(chain, parseName("VST3:Plug"), 1) == -1);
    CHECK(resolveInsert(chain, parseName("FXADD:1"), 1) == -1);
    CHECK(resolveInsert(chain, parseName(""), 1) == -1);

    if (fails == 0) std::printf("OK: test_fx_rules\n");
    return fails == 0 ? 0 : 1;
}
