from afterline.processing import format_transcript

def test_timestamps_are_sorted():
    result = format_transcript([
        {"start": 61.2, "source": "Microphone", "text": "Second"},
        {"start": 1, "source": "Computer", "text": "First"}])
    assert result.splitlines() == [
        "[00:00:01] Computer: First", "[00:01:01] Microphone: Second"]

def test_empty_transcript():
    assert format_transcript([]) == ""
