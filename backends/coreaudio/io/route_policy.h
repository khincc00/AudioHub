// Aturan route virtual → virtual. Tanpa CoreAudio, supaya bisa diuji offline.
//
// Urutan kanal di agregat: kanal fisik (master), lalu kanal virtual 1, lalu kanal virtual 2.
// Kanal virtual dari satu perangkat tidak bisa dihubungkan ke kanal virtual dari perangkat yang
// sama, karena driver akan meloopback output ke input. Antar dua perangkat virtual berbeda
// aman, karena masing-masing punya buffer sendiri.
#ifndef AUDIOHUB_ROUTE_POLICY_H
#define AUDIOHUB_ROUTE_POLICY_H

namespace audiohub {

// Pemilik kanal: 0 = fisik, 1 = virtual 1, 2 = virtual 2.
// virt1Ch = jumlah kanal virtual 1 pada sisi yang sama (input atau output).
inline int channelOwner(int ch, int physCh, int virt1Ch) {
    if (ch < physCh) return 0;
    return (ch - physCh) < virt1Ch ? 1 : 2;
}

// Layout kanal satu sisi. physCh = kanal fisik, virt1Ch = kanal virtual 1, virt2Ch = kanal virtual 2.
struct ChannelLayout {
    int physIn, virt1In, virt2In;
    int physOut, virt1Out, virt2Out;
};

// Mengembalikan true jika route dari input ke output boleh dibuat.
// Indeks di luar jumlah kanal ditolak. Route dari fisik ke mana saja, atau dari virtual ke fisik,
// selalu boleh. Route dari virtual ke virtual hanya boleh jika pemilik kedua ujung berbeda.
inline bool routeAllowed(int input, int output, const ChannelLayout& L) {
    int nIn = L.physIn + L.virt1In + L.virt2In;
    int nOut = L.physOut + L.virt1Out + L.virt2Out;
    if (input < 0 || input >= nIn || output < 0 || output >= nOut) return false;
    bool inVirtual = input >= L.physIn;
    bool outVirtual = output >= L.physOut;
    if (!inVirtual || !outVirtual) return true;
    return channelOwner(input, L.physIn, L.virt1In) != channelOwner(output, L.physOut, L.virt1Out);
}

}  // namespace audiohub

#endif
