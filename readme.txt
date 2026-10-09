========================================================================
    Aster Player
========================================================================

A desktop video player built with standard C++, C++/WinRT, WinUI 3, and the
Windows App SDK. Playback uses the Windows Media Player engine supplied by
the platform; supported formats and codecs therefore depend on Windows and
the codecs installed on the computer.

Features:
* Add multiple local video and audio files or UTF-8 M3U/M3U8 playlists to a
  playback queue, open HTTP/HTTPS streams, or drag files into the player.
  Shuffle, repeat, previous/next, remove selected, and clear the queue.
* Play, pause, stop, seek, and jump forward/backward; use Space, arrow keys,
  M, and F for common playback, seek, volume, mute, and full-screen actions.
* Adjust playback speed, volume, mute, and stereo balance.
* Select embedded caption/subtitle tracks or load external SRT, WebVTT, and
  TTML subtitle files.
* Toggle a compact theater layout, use full-window video, and follow playback
  position and duration.
* Native WinUI controls and subtle entrance transitions for queue and
  drag-and-drop states.

To build:
1. Open App1.slnx in Visual Studio with the C++ and Windows App SDK
   workloads installed.
2. Build the Debug or Release x64 configuration.
3. Run the application and use the Open media control or drag media files
   into the window.

This player keeps the native Windows playback stack and does not aim to
duplicate VLC's bundled codec coverage or every advanced VLC feature.
Codec and subtitle support depends on Windows and the installed media
components.

========================================================================
Learn more:
https://learn.microsoft.com/windows/apps/windows-app-sdk/
https://learn.microsoft.com/windows/apps/winui/winui3/
https://learn.microsoft.com/windows/uwp/audio-video-camera/media-playback
========================================================================
