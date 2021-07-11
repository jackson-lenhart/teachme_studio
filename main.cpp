struct app_state {
  bool debug_render_hello;
};

app_state STATE = {};

void update_and_render(win32_offscreen_buffer *buffer, game_assets *assets, keyboard_input *k_input) {
  if (k_input->return_pressed) STATE.debug_render_hello = !STATE.debug_render_hello;

  debug_paint_window(0x434591);
  if (STATE.debug_render_hello) debug_render_string(300, 300, buffer, "HELLO");
}
