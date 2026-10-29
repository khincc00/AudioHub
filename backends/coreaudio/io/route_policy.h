// Aturan route virtual → virtual. Tanpa CoreAudio, supaya bisa diuji offline.
//
// Urutan kanal di agregat: kanal fisik (master), lalu kanal virtual 1, virtual 2, virtual 3 (loopback).
// Kanal virtual dari satu perangkat boleh dihubungkan ke kanal virtual dari perangkat yang sama.
// Karena driver meloopback output ke input, route itu membentuk loop; lihat routeLoops.
#ifndef AUDIOHUB_ROUTE_POLICY_H
#define AUDIOHUB_ROUTE_POLICY_H

namespace audiohub {

// Pemilik kanal: 0 = fisik, 1 = virtual 1, 2 = virtual 2, 3 = virtual 3 (loopback), 4 = tap aplikasi (input saja).
// Argumen virtN = jumlah kanal tiap sumber pada sisi yang sama. Indeks di luar jumlah kanal mengembalikan 4;
// pemanggil harus memeriksa batasnya.
inline int channelOwner(int ch, int physCh, int virt1Ch, int virt2Ch, int virt3Ch) {
    if (ch < physCh) return 0;
    int c = ch - physCh;
    if (c < virt1Ch) return 1;
    c -= virt1Ch;
    if (c < virt2Ch) return 2;
    c -= virt2Ch;
    if (c < virt3Ch) return 3;
    return 4;
}

// Layout kanal per sisi. physX = kanal fisik, virtNX = kanal virtual device ke-N.
// tapIn = jumlah kanal input dari tap aplikasi. Tap hanya ada di sisi input.
struct ChannelLayout {
    int physIn, virt1In, virt2In, virt3In;
    int physOut, virt1Out, virt2Out, virt3Out;
    int tapIn = 0;
};

// Mengembalikan true jika route dari input ke output boleh dibuat. Hanya indeks yang diperiksa.
// Route virtual → virtual dalam perangkat yang sama boleh, tetapi membentuk loop (lihat routeLoops).
inline bool routeAllowed(int input, int output, const ChannelLayout& L) {
    int nIn = L.physIn + L.virt1In + L.virt2In + L.virt3In + L.tapIn;
    int nOut = L.physOut + L.virt1Out + L.virt2Out + L.virt3Out;
    return input >= 0 && input < nIn && output >= 0 && output < nOut;
}

// Route dari virtual ke virtual dalam perangkat yang sama membentuk loop: output driver kembali ke input.
// Tap aplikasi tidak pernah dihitung sebagai loop.
inline bool routeLoops(int input, int output, const ChannelLayout& L) {
    if (!routeAllowed(input, output, L)) return false;
    int inOwner = channelOwner(input, L.physIn, L.virt1In, L.virt2In, L.virt3In);
    int outOwner = channelOwner(output, L.physOut, L.virt1Out, L.virt2Out, L.virt3Out);
    if (inOwner == 0 || inOwner == 4 || outOwner == 0) return false;
    return inOwner == outOwner;
}

}  // namespace audiohub

#endif
