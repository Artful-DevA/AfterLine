from unittest.mock import patch
from afterline.audio import sources, microphones

@patch("afterline.audio.pactl")
def test_streams_are_listed(mock_pactl):
    mock_pactl.side_effect = lambda name: (
        [{"name":"alsa_output.monitor","description":"Speakers"},
         {"name":"alsa_input.microphone","description":"Mic"}]
        if name == "sources" else
        [{"index":34, "properties":{"application.name":"Firefox"}}])
    assert any("Firefox" in label for label, _ in sources())
    assert any("Mic" in label for label, _ in microphones())
