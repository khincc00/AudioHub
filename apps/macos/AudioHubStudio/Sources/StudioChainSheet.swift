import SwiftUI

// Editor rantai FX per track. Dibuka dari tombol FX di setiap input pada layar AudioHub.
struct StudioChainSheet: View {
    @ObservedObject var engine: EngineController
    @ObservedObject var chains: StudioChains

    var body: some View {
        let i = chains.selectedInput
        let visible = chains.chain(i)
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                Text("Rantai FX").font(.headline)
                Picker("Track", selection: $chains.selectedInput) {
                    ForEach(0..<engine.inputs, id: \.self) { j in
                        Text(engine.inLabel(j)).tag(j)
                    }
                }
                .labelsHidden()
                .frame(width: 180)
                Spacer()
                Menu("Tambah FX") {
                    ForEach(engine.effectNames, id: \.self) { name in
                        Button(name) { chains.addEffect(name) }
                    }
                }
                .disabled(engine.effectNames.isEmpty || !engine.isOpen)
                Button("Selesai") { chains.editorPresented = false }
                    .keyboardShortcut(.defaultAction)
            }

            if !engine.isOpen {
                Text("Perangkat belum terbuka. Tekan Mulai di jendela utama dulu.")
                    .font(.callout).foregroundStyle(.secondary)
            } else if visible.isEmpty {
                Text("Belum ada FX di track ini. Pilih Tambah FX untuk memasang Audio Unit.")
                    .font(.callout).foregroundStyle(.secondary)
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(Array(visible.enumerated()), id: \.element) { position, k in
                        ChainRow(chains: chains, slot: k, position: position, count: visible.count)
                    }
                }
            }
            .frame(maxHeight: 200)

            Divider()
            ParamPanel(chains: chains)
            Spacer(minLength: 0)
        }
        .padding(16)
    }
}

struct ChainRow: View {
    @ObservedObject var chains: StudioChains
    let slot: Int
    let position: Int
    let count: Int

    var body: some View {
        let state = chains.slots[chains.selectedInput][slot]
        let isSelected = chains.selectedSlot == slot
        HStack(spacing: 8) {
            Toggle("", isOn: Binding(get: { state.enabled }, set: { chains.setEnabled(slot, $0) }))
                .labelsHidden()
                .toggleStyle(.switch)
                .controlSize(.small)
                .help("Aktif / bypass")
            VStack(alignment: .leading, spacing: 2) {
                Text(state.name).lineLimit(1)
                if !state.loaded {
                    Text("offline: plugin tidak termuat")
                        .font(.caption)
                        .foregroundStyle(.orange)
                }
            }
            Spacer()
            Button { chains.move(slot, by: -1) } label: { Image(systemName: "arrow.up") }
                .disabled(position == 0)
                .help("Naikkan urutan")
            Button { chains.move(slot, by: 1) } label: { Image(systemName: "arrow.down") }
                .disabled(position == count - 1)
                .help("Turunkan urutan")
            Button { chains.select(isSelected ? nil : slot) } label: {
                Image(systemName: "slider.horizontal.3").foregroundStyle(isSelected ? Style.accent : .primary)
            }
            .help("Parameter")
            Button(role: .destructive) { chains.removeEffect(slot) } label: { Image(systemName: "trash") }
                .help("Lepas FX")
        }
        .buttonStyle(.borderless)
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 6).fill(isSelected ? Style.accent.opacity(0.14) : Color.secondary.opacity(0.08)))
    }
}

struct ParamPanel: View {
    @ObservedObject var chains: StudioChains

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if chains.selectedSlot == nil {
                Text("Pilih ikon parameter pada FX untuk mengubah nilainya.")
                    .font(.callout).foregroundStyle(.secondary)
            } else if chains.params.isEmpty {
                Text("Tidak ada parameter, atau FX belum termuat.")
                    .font(.callout).foregroundStyle(.secondary)
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(chains.params) { row in
                            ParamRowView(chains: chains, row: row)
                        }
                    }
                }
                .frame(maxHeight: 200)
            }
        }
    }
}

struct ParamRowView: View {
    @ObservedObject var chains: StudioChains
    let row: FxParamRow

    var body: some View {
        HStack(spacing: 10) {
            Text(row.name)
                .lineLimit(1)
                .frame(width: 200, alignment: .leading)
            if row.maxValue > row.minValue {
                Slider(
                    value: Binding(get: { Double(row.value) }, set: { chains.setParam(row.id, Float($0)) }),
                    in: Double(row.minValue)...Double(row.maxValue)
                )
                .tint(Style.accent)
            } else {
                Spacer()
            }
            Text(String(format: "%.3g", row.value))
                .monospacedDigit()
                .frame(width: 64, alignment: .trailing)
        }
    }
}
