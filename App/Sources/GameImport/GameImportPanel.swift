import SwiftUI
import UniformTypeIdentifiers

/// The "Import Your Game" panel (Milestone G1). Uses the iPadOS Files picker; see GameLibrary.
struct GameImportPanel: View {
    @ObservedObject private var library = GameLibrary.shared
    @State private var picking = false
    @State private var pickingImage = false
    @State private var showDetails = true

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Import Your Game")
                .font(.headline)
            Text("Your own Xbox 360 copy of NFS Most Wanted (2012). Files stay on this iPad and are only read.")
                .font(.caption)
                .fixedSize(horizontal: false, vertical: true)
            Divider()
            Text(library.title)
                .font(.system(.body, design: .monospaced).bold())
            if !library.folderName.isEmpty {
                Text("Folder: \(library.folderName)")
                    .font(.caption)
            }
            if !library.message.isEmpty {
                Text(library.message)
                    .font(.callout)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if library.busy {
                ProgressView()
            }
            HStack {
                Button("Choose Game Folder") {
                    pickingImage = false
                    picking = true
                }
                .buttonStyle(.borderedProminent)
                Button("Choose Disc Image") {
                    pickingImage = true
                    picking = true
                }
                .buttonStyle(.bordered)
            }
            .disabled(library.busy)
            HStack {
                Button("Verify Again") { library.verifyAgain() }
                Button("Forget Folder") { library.forget() }
            }
            .buttonStyle(.bordered)
            .disabled(library.busy)

            Divider()
            Text("Testing (synthetic data, no game content)\(library.testManifestActive ? " — test manifest ON" : "")")
                .font(.caption)
            HStack {
                Button("Importer self-test") { library.runSelfTest() }
                Menu("Make test folder") {
                    Button("Complete") { library.makeTestFolder(variant: 0) }
                    Button("One file missing") { library.makeTestFolder(variant: 1) }
                    Button("One file damaged") { library.makeTestFolder(variant: 2) }
                    Button("A different game") { library.makeTestFolder(variant: 3) }
                    Button("Remove test data", role: .destructive) { library.removeTestData() }
                }
            }
            .buttonStyle(.bordered)
            .disabled(library.busy)

            if !library.details.isEmpty {
                Button(showDetails ? "Hide details" : "Show details (\(library.details.count))") {
                    showDetails.toggle()
                }
                .font(.caption)
                if showDetails {
                    ForEach(Array(library.details.suffix(22).enumerated()), id: \.offset) { _, line in
                        Text(line)
                            .font(.system(.caption2, design: .monospaced))
                            .lineLimit(3)
                    }
                }
            }
        }
        .frame(width: 430, alignment: .leading)
        .foregroundStyle(.white)
        .padding(14)
        .background(Color.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 10))
        .fileImporter(isPresented: $picking,
                      allowedContentTypes: pickingImage ? [.data] : [.folder],
                      allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                if pickingImage {
                    library.choseImage(url)
                } else {
                    library.choseFolder(url)
                }
            case .failure(let error):
                ShellLog.shared.write("[import] Picker failed: \(error.localizedDescription)")
            }
        }
        .onAppear { library.restore() }
    }
}
