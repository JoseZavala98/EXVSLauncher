#include "webview/webview.h"
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  webview::webview w(false, nullptr);
  w.set_title("hello"); w.set_size(480, 320, WEBVIEW_HINT_NONE);
  w.bind("ping", [](const std::string &req) -> std::string { return "\"pong\""; });
  w.set_html("<h1>hi</h1>");
  w.run();
  return 0;
}
