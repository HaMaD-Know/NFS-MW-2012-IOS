// Option A: compiled together with the game's launcher code (ReXGlue-generated, with its
// precompiled header included first) into libnfsmw_default.dylib. The app's host bridge loads that
// library and asks it for the launcher's image (RexHeadlessHost.cpp, GameImage). No game code here.
extern "C" __attribute__((visibility("default"))) const rex::ppc::PPCImage *nfsmw_game_image() {
  return &kPPCImageConfig;
}
