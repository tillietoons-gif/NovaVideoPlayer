#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <chrono>
#include <commdlg.h>
#include <cwctype>
#include <cwctype>
#include <cwchar>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>
#include <unordered_set>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Automation;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::Playback;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage;

namespace winrt::App1::implementation
{
    int32_t MainWindow::MyProperty()
    {
        return 0;
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();

        m_mediaPlayer = MediaPlayer();
        m_mediaPlayer.Volume(0.7);
        this->VideoPlayer().SetMediaPlayer(m_mediaPlayer);
        this->PlaylistView().ItemsSource(m_playlistItems = single_threaded_observable_vector<hstring>());
        this->VolumeSlider().Value(70);
        this->AudioBalanceSlider().Value(0);
        this->MuteButton().IsChecked(false);

        m_mediaPlayer.MediaFailed([this](MediaPlayer const&, MediaPlayerFailedEventArgs const& args)
        {
            this->StatusText().Text(L"Unable to play video: " + args.ErrorMessage());
        });

        m_mediaPlayer.MediaEnded([this](MediaPlayer const&, IInspectable const&)
        {
            auto repeat = this->RepeatButton().IsChecked();
            if (repeat && repeat.Value())
            {
                m_mediaPlayer.PlaybackSession().Position(Windows::Foundation::TimeSpan{ 0 });
                m_mediaPlayer.Play();
            }
            else
            {
                AdvancePlaylist(false);
            }
        });

        m_timer = DispatcherTimer();
        m_timer.Interval(std::chrono::milliseconds(250));
        m_timer.Tick([this](IInspectable const&, IInspectable const&)
        {
            UpdateTimeline();
            UpdatePlaybackState();
            RefreshSubtitleTracks();
        });
        m_timer.Start();

        this->StatusText().Text(L"Ready when you are");
        this->QueueCountText().Text(L"0 videos");
        this->NowPlayingText().Text(L"Choose a video");
    }

    hstring MainWindow::NormalizeFilePathForUri(std::wstring const& sourcePath)
    {
        if (sourcePath.starts_with(L"http://") || sourcePath.starts_with(L"https://"))
        {
            return hstring(sourcePath);
        }

        std::wstring normalized = sourcePath;
        std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
        return hstring(L"file:///" + normalized);
    }

    hstring MainWindow::FormatTime(double seconds)
    {
        auto totalSeconds = static_cast<int64_t>(std::max(0.0, seconds));
        auto hours = totalSeconds / 3600;
        auto minutes = (totalSeconds % 3600) / 60;
        auto remainingSeconds = totalSeconds % 60;

        std::wostringstream formatted;
        if (hours > 0)
        {
            formatted << hours << L":";
        }
        formatted << std::setfill(L'0') << std::setw(2) << minutes << L":"
                  << std::setw(2) << remainingSeconds;
        return hstring(formatted.str());
    }

    void MainWindow::OpenVideo_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::vector<wchar_t> selectedFiles(65536, L'\0');
        OPENFILENAMEW openFileDialog{};
        openFileDialog.lStructSize = sizeof(openFileDialog);
        openFileDialog.lpstrFile = selectedFiles.data();
        openFileDialog.nMaxFile = static_cast<DWORD>(selectedFiles.size());
        openFileDialog.lpstrFilter =
            L"Media and playlists (*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.flv;*.ts;*.m2ts;*.mpeg;*.mpg;*.3gp;*.asf;*.mp3;*.wav;*.wma;*.m4a;*.aac;*.flac;*.ogg;*.opus;*.aiff;*.m3u;*.m3u8)\0"
            L"*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.flv;*.ts;*.m2ts;*.mpeg;*.mpg;*.3gp;*.asf;*.mp3;*.wav;*.wma;*.m4a;*.aac;*.flac;*.ogg;*.opus;*.aiff;*.m3u;*.m3u8\0"
            L"All files (*.*)\0*.*\0";
        openFileDialog.nFilterIndex = 1;
        openFileDialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        if (!GetOpenFileNameW(&openFileDialog))
        {
            auto dialogError = CommDlgExtendedError();
            if (dialogError != 0)
            {
                auto message = std::wstring(L"Could not open file picker (error ");
                message += std::to_wstring(static_cast<uint32_t>(dialogError));
                message += L")";
                this->StatusText().Text(hstring(message));
            }
            return;
        }

        std::vector<std::wstring> selectedPaths;
        auto firstPath = std::wstring(selectedFiles.data());
        auto offset = firstPath.size() + 1;
        if (offset >= selectedFiles.size() || selectedFiles[offset] == L'\0')
        {
            selectedPaths.push_back(std::move(firstPath));
        }
        else
        {
            while (offset < selectedFiles.size() && selectedFiles[offset] != L'\0')
            {
                std::wstring path = firstPath;
                path += L'\\';
                path += selectedFiles.data() + offset;
                selectedPaths.push_back(std::move(path));
                offset += std::wcslen(selectedFiles.data() + offset) + 1;
            }
        }

        QueueMediaFiles(selectedPaths);
    }

    fire_and_forget MainWindow::OpenStream_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto lifetime = get_strong();
        ContentDialog dialog;
        dialog.XamlRoot(this->RootGrid().XamlRoot());
        dialog.Title(box_value(L"Open network stream"));
        dialog.PrimaryButtonText(L"Play");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);

        TextBox address;
        address.PlaceholderText(L"https://example.com/video.m3u8");
        address.AcceptsReturn(false);
        address.Header(box_value(L"Stream URL"));
        dialog.Content(address);

        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto url = address.Text();
        if (!url.starts_with(L"http://") && !url.starts_with(L"https://"))
        {
            this->StatusText().Text(L"Enter a valid HTTP or HTTPS stream URL");
            co_return;
        }

        QueueMediaFiles({ std::wstring(url) });
    }

    void MainWindow::QueueMediaFiles(std::vector<std::wstring> const& paths)
    {
        auto const wasEmpty = m_playlist.empty();
        size_t addedCount = 0;
        size_t subtitleCount = 0;
        std::vector<std::wstring> subtitlePaths;
        std::vector<std::wstring> mediaPaths;
        std::unordered_set<std::wstring> seenPlaylists;

        for (auto const& path : paths)
        {
            if (path.starts_with(L"http://") || path.starts_with(L"https://"))
            {
                mediaPaths.push_back(path);
                continue;
            }

            auto extensionStart = path.find_last_of(L'.');
            std::wstring extension = extensionStart == std::wstring::npos ? L"" : path.substr(extensionStart);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });

            if (extension != L".m3u" && extension != L".m3u8")
            {
                mediaPaths.push_back(path);
                continue;
            }

            if (!seenPlaylists.insert(path).second)
            {
                continue;
            }

            std::ifstream playlist(path, std::ios::binary);
            if (!playlist)
            {
                this->StatusText().Text(L"Could not read playlist: " + hstring(path));
                continue;
            }

            std::string contents((std::istreambuf_iterator<char>(playlist)), std::istreambuf_iterator<char>());
            if (contents.size() > 8 * 1024 * 1024)
            {
                this->StatusText().Text(L"Playlist is too large to open");
                continue;
            }

            auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                contents.data(), static_cast<int>(contents.size()), nullptr, 0);
            if (required <= 0)
            {
                this->StatusText().Text(L"Playlist must be valid UTF-8");
                continue;
            }

            std::wstring decoded(static_cast<size_t>(required), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                contents.data(), static_cast<int>(contents.size()), decoded.data(), required);

            auto directoryEnd = path.find_last_of(L"\\/");
            auto playlistDirectory = directoryEnd == std::wstring::npos ? std::wstring{} : path.substr(0, directoryEnd + 1);
            std::wistringstream lines(decoded);
            std::wstring line;
            while (std::getline(lines, line))
            {
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }
                if (!line.empty() && line.front() == L'\uFEFF')
                {
                    line.erase(line.begin());
                }

                auto first = line.find_first_not_of(L" \t");
                auto last = line.find_last_not_of(L" \t");
                if (first == std::wstring::npos || line[first] == L'#')
                {
                    continue;
                }
                line = line.substr(first, last - first + 1);
                if (line.starts_with(L"http://") || line.starts_with(L"https://") ||
                    (line.size() > 2 && line[1] == L':') || line.starts_with(L"\\\\"))
                {
                    mediaPaths.push_back(line);
                }
                else
                {
                    mediaPaths.push_back(playlistDirectory + line);
                }
            }
        }

        for (auto const& path : mediaPaths)
        {
            auto extensionStart = path.find_last_of(L'.');
            std::wstring extension = extensionStart == std::wstring::npos ? L"" : path.substr(extensionStart);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });

            if (extension == L".srt" || extension == L".vtt" || extension == L".ttml")
            {
                subtitlePaths.push_back(path);
                continue;
            }

            static constexpr wchar_t const* supportedExtensions[] = {
                L".mp4", L".m4v", L".mov", L".wmv", L".avi", L".mkv", L".webm", L".flv",
                L".ts", L".m2ts", L".vob", L".ogv",
                L".mpeg", L".mpg", L".3gp", L".asf", L".mp3", L".wav",
                L".wma", L".m4a", L".aac", L".flac", L".ogg", L".opus", L".aiff", L".ac3"
            };
            auto const isNetworkStream =
                path.starts_with(L"http://") || path.starts_with(L"https://");
            if (!isNetworkStream &&
                std::find_if(std::begin(supportedExtensions), std::end(supportedExtensions),
                    [&extension](wchar_t const* supported) { return extension == supported; }) ==
                    std::end(supportedExtensions))
            {
                continue;
            }

            m_playlist.push_back(path);
            auto nameStart = path.find_last_of(L"\\/");
            auto fileName = nameStart == std::wstring::npos ? path : path.substr(nameStart + 1);
            m_playlistItems.Append(hstring(fileName));
            ++addedCount;
        }

        this->QueueCountText().Text(to_hstring(m_playlist.size()) +
            (m_playlist.size() == 1 ? L" item" : L" items"));
        this->ClearQueueButton().IsEnabled(!m_playlist.empty());
        this->RemoveQueueItemButton().IsEnabled(!m_playlist.empty());
        this->QueueEmptyState().Visibility(m_playlist.empty() ? Visibility::Visible : Visibility::Collapsed);
        if (wasEmpty && !m_playlist.empty())
        {
            this->PlaylistView().SelectedIndex(0);
        }
        else if (addedCount > 0)
        {
            this->StatusText().Text(to_hstring(addedCount) + L" media item(s) added to queue");
        }
        for (auto const& path : subtitlePaths)
        {
            if (AddSubtitleFile(path))
            {
                ++subtitleCount;
            }
        }
        if (subtitleCount > 0)
        {
            this->StatusText().Text(to_hstring(subtitleCount) + L" subtitle file(s) loaded");
        }
        else if (addedCount == 0 && subtitlePaths.empty() && !paths.empty())
        {
            this->StatusText().Text(L"No supported media or subtitle files found");
        }
    }

    bool MainWindow::AddSubtitleFile(std::wstring const& path)
    {
        if (!m_currentMediaSource)
        {
            this->StatusText().Text(L"Open a media item before loading subtitles");
            return false;
        }

        try
        {
            auto subtitleSource = TimedTextSource::CreateFromUri(Uri(NormalizeFilePathForUri(path)));
            m_currentMediaSource.ExternalTimedTextSources().Append(subtitleSource);
            this->StatusText().Text(L"Subtitle added - choose it from the subtitle menu");
            return true;
        }
        catch (hresult_error const& error)
        {
            this->StatusText().Text(L"Could not load subtitle: " + error.message());
            return false;
        }
    }

    void MainWindow::Playlist_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_updatingPlaylistSelection)
        {
            return;
        }

        auto selectedIndex = this->PlaylistView().SelectedIndex();
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < m_playlist.size())
        {
            PlayIndex(static_cast<size_t>(selectedIndex));
        }
    }

    void MainWindow::PlayIndex(size_t index)
    {
        if (index >= m_playlist.size())
        {
            return;
        }

        m_currentIndex = index;
        if (this->PlaylistView().SelectedIndex() != static_cast<int32_t>(index))
        {
            m_updatingPlaylistSelection = true;
            this->PlaylistView().SelectedIndex(static_cast<int32_t>(index));
            m_updatingPlaylistSelection = false;
        }

        auto filePath = m_playlist[index];
        m_currentMediaSource = MediaSource::CreateFromUri(Uri(NormalizeFilePathForUri(filePath)));
        m_currentPlaybackItem = MediaPlaybackItem(m_currentMediaSource);
        m_subtitleTrackIndices.clear();
        m_updatingSubtitlePicker = true;
        this->SubtitlePicker().Items().Clear();
        this->SubtitlePicker().Items().Append(box_value(L"Subtitles off"));
        this->SubtitlePicker().SelectedIndex(0);
        m_updatingSubtitlePicker = false;
        m_mediaPlayer.Source(m_currentPlaybackItem);
        this->EmptyState().Visibility(Visibility::Collapsed);
        this->NowPlayingText().Text(hstring(m_playlistItems.GetAt(static_cast<uint32_t>(index))));
        this->StatusText().Text(L"Playing");
        RefreshSubtitleTracks();
        m_mediaPlayer.Play();
        UpdatePlaybackState();
        UpdateTimeline();
    }

    void MainWindow::PlayPause_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_playlist.empty())
        {
            this->StatusText().Text(L"Add a video to the queue first");
            return;
        }

        if (m_mediaPlayer.PlaybackSession().PlaybackState() == MediaPlaybackState::Playing)
        {
            m_mediaPlayer.Pause();
            this->StatusText().Text(L"Paused");
        }
        else
        {
            m_mediaPlayer.Play();
            this->StatusText().Text(L"Playing");
        }
        UpdatePlaybackState();
    }

    void MainWindow::Stop_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_playlist.empty())
        {
            return;
        }

        m_mediaPlayer.Pause();
        m_mediaPlayer.PlaybackSession().Position(Windows::Foundation::TimeSpan{ 0 });
        this->StatusText().Text(L"Stopped");
        UpdatePlaybackState();
        UpdateTimeline();
    }

    void MainWindow::Previous_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_playlist.empty())
        {
            return;
        }
        if (m_mediaPlayer.PlaybackSession().Position().count() > 30'000'000)
        {
            m_mediaPlayer.PlaybackSession().Position(Windows::Foundation::TimeSpan{ 0 });
            m_mediaPlayer.Play();
            return;
        }
        AdvancePlaylist(true);
    }

    void MainWindow::Next_Click(IInspectable const&, RoutedEventArgs const&)
    {
        AdvancePlaylist(false);
    }

    void MainWindow::AdvancePlaylist(bool backwards)
    {
        if (m_playlist.empty())
        {
            return;
        }

        if (backwards)
        {
            if (m_currentIndex == 0)
            {
                return;
            }
            PlayIndex(m_currentIndex - 1);
            return;
        }

        if (m_shuffleEnabled && m_playlist.size() > 1)
        {
            std::uniform_int_distribution<size_t> chooseTrack(0, m_playlist.size() - 2);
            auto nextIndex = chooseTrack(m_random);
            if (nextIndex >= m_currentIndex)
            {
                ++nextIndex;
            }
            PlayIndex(nextIndex);
            return;
        }

        if (m_currentIndex + 1 >= m_playlist.size())
        {
            this->StatusText().Text(L"End of queue");
            m_mediaPlayer.Pause();
            UpdatePlaybackState();
            return;
        }
        PlayIndex(m_currentIndex + 1);
    }

    void MainWindow::SeekBackward_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SeekBy(-10);
    }

    void MainWindow::SeekForward_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SeekBy(10);
    }

    void MainWindow::SeekBy(double seconds)
    {
        auto session = m_mediaPlayer.PlaybackSession();
        if (!session || !session.CanSeek())
        {
            return;
        }

        auto position = session.Position().count() / 10'000'000.0 + seconds;
        auto duration = session.NaturalDuration().count() / 10'000'000.0;
        position = std::clamp(position, 0.0, std::max(0.0, duration));
        session.Position(Windows::Foundation::TimeSpan{ static_cast<int64_t>(position * 10'000'000.0) });
        UpdateTimeline();
    }

    void MainWindow::FullWindow_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto fullWindow = !this->VideoPlayer().IsFullWindow();
        this->VideoPlayer().IsFullWindow(fullWindow);
        this->FullWindowButton().Icon(SymbolIcon(fullWindow ? Symbol::BackToWindow : Symbol::FullScreen));
        AutomationProperties::SetName(this->FullWindowButton(), fullWindow ? L"Exit full screen" : L"Full screen");
    }

    void MainWindow::Mute_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_mediaPlayer.IsMuted(!m_mediaPlayer.IsMuted());
        auto isMuted = m_mediaPlayer.IsMuted();
        this->MuteButton().IsChecked(isMuted);
        this->MuteButton().Icon(SymbolIcon(isMuted ? Symbol::Mute : Symbol::Volume));
        AutomationProperties::SetName(this->MuteButton(), isMuted ? L"Unmute" : L"Mute");
    }

    void MainWindow::Repeat_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto isRepeating = this->RepeatButton().IsChecked().Value();
        this->StatusText().Text(isRepeating ? L"Repeating current video" : L"Repeat off");
    }

    void MainWindow::Shuffle_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_shuffleEnabled = this->ShuffleButton().IsChecked().Value();
        this->StatusText().Text(m_shuffleEnabled ? L"Shuffle enabled" : L"Shuffle disabled");
    }

    void MainWindow::ClearQueue_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_mediaPlayer.Pause();
        m_mediaPlayer.Source(Windows::Media::Playback::IMediaPlaybackSource{ nullptr });
        m_currentPlaybackItem = nullptr;
        m_currentMediaSource = nullptr;
        m_playlist.clear();
        m_playlistItems.Clear();
        m_currentIndex = 0;

        m_updatingPlaylistSelection = true;
        this->PlaylistView().SelectedIndex(-1);
        m_updatingPlaylistSelection = false;

        this->QueueCountText().Text(L"0 videos");
        this->ClearQueueButton().IsEnabled(false);
        this->RemoveQueueItemButton().IsEnabled(false);
        this->QueueEmptyState().Visibility(Visibility::Visible);
        this->EmptyState().Visibility(Visibility::Visible);
        this->NowPlayingText().Text(L"Choose a video");
        this->StatusText().Text(L"Queue cleared");
        this->CurrentTimeText().Text(L"00:00");
        this->DurationText().Text(L"00:00");
        this->SeekSlider().Value(0);
        this->SeekSlider().IsEnabled(false);
        m_subtitleTrackIndices.clear();
        m_updatingSubtitlePicker = true;
        this->SubtitlePicker().Items().Clear();
        this->SubtitlePicker().Items().Append(box_value(L"Subtitles off"));
        this->SubtitlePicker().SelectedIndex(0);
        m_updatingSubtitlePicker = false;
        UpdatePlaybackState();
    }

    void MainWindow::RemoveQueueItem_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selectedIndex = this->PlaylistView().SelectedIndex();
        if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= m_playlist.size())
        {
            return;
        }

        auto const removedIndex = static_cast<size_t>(selectedIndex);
        auto const removedCurrent = removedIndex == m_currentIndex;
        m_updatingPlaylistSelection = true;
        m_playlist.erase(m_playlist.begin() + removedIndex);
        m_playlistItems.RemoveAt(static_cast<uint32_t>(removedIndex));
        m_updatingPlaylistSelection = false;

        if (m_playlist.empty())
        {
            ClearQueue_Click(nullptr, RoutedEventArgs{ nullptr });
            return;
        }

        this->QueueCountText().Text(to_hstring(m_playlist.size()) +
            (m_playlist.size() == 1 ? L" item" : L" items"));
        this->ClearQueueButton().IsEnabled(true);
        this->RemoveQueueItemButton().IsEnabled(true);
        this->QueueEmptyState().Visibility(Visibility::Collapsed);

        if (removedCurrent)
        {
            auto nextIndex = std::min(removedIndex, m_playlist.size() - 1);
            PlayIndex(nextIndex);
            return;
        }

        if (removedIndex < m_currentIndex)
        {
            --m_currentIndex;
        }

        auto selectedAfterRemoval = std::min(m_currentIndex, m_playlist.size() - 1);
        m_updatingPlaylistSelection = true;
        this->PlaylistView().SelectedIndex(static_cast<int32_t>(selectedAfterRemoval));
        m_updatingPlaylistSelection = false;
        this->StatusText().Text(L"Removed from queue");
    }

    void MainWindow::OpenSubtitle_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_currentMediaSource)
        {
            this->StatusText().Text(L"Open a media item before loading subtitles");
            return;
        }

        std::vector<wchar_t> selectedFile(32768, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFile = selectedFile.data();
        dialog.nMaxFile = static_cast<DWORD>(selectedFile.size());
        dialog.lpstrFilter = L"Subtitle files (*.srt;*.vtt;*.ttml)\0*.srt;*.vtt;*.ttml\0All files (*.*)\0*.*\0";
        dialog.nFilterIndex = 1;
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        if (GetOpenFileNameW(&dialog))
        {
            AddSubtitleFile(selectedFile.data());
        }
        else if (auto error = CommDlgExtendedError(); error != 0)
        {
            this->StatusText().Text(L"Could not open subtitle picker (error " +
                to_hstring(static_cast<uint32_t>(error)) + L")");
        }
    }

    void MainWindow::RootGrid_DragOver(IInspectable const&, DragEventArgs const& args)
    {
        auto data = args.DataView();
        if (data.Contains(StandardDataFormats::StorageItems()))
        {
            args.AcceptedOperation(DataPackageOperation::Copy);
        }
        else
        {
            args.AcceptedOperation(DataPackageOperation::None);
        }
        args.Handled(true);
    }

    void MainWindow::RootGrid_DragEnter(IInspectable const&, DragEventArgs const& args)
    {
        if (args.DataView().Contains(StandardDataFormats::StorageItems()))
        {
            this->DropOverlay().Visibility(Visibility::Visible);
            args.AcceptedOperation(DataPackageOperation::Copy);
        }
    }

    void MainWindow::RootGrid_DragLeave(IInspectable const&, DragEventArgs const&)
    {
        this->DropOverlay().Visibility(Visibility::Collapsed);
    }

    fire_and_forget MainWindow::RootGrid_Drop(IInspectable const&, DragEventArgs const& args)
    {
        auto lifetime = get_strong();
        auto dropEvent = args;
        this->DropOverlay().Visibility(Visibility::Collapsed);

        try
        {
            auto data = dropEvent.DataView();
            if (!data.Contains(StandardDataFormats::StorageItems()))
            {
                co_return;
            }

            auto storageItems = co_await data.GetStorageItemsAsync();
            std::vector<std::wstring> paths;
            for (auto const& item : storageItems)
            {
                if (item.IsOfType(StorageItemTypes::File))
                {
                    paths.emplace_back(item.as<StorageFile>().Path().c_str());
                }
            }

            QueueMediaFiles(paths);
            dropEvent.AcceptedOperation(DataPackageOperation::Copy);
            dropEvent.Handled(true);
        }
        catch (hresult_error const& error)
        {
            this->StatusText().Text(L"Could not read dropped files: " + error.message());
        }
    }

    void MainWindow::CompactMode_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_compactMode = this->CompactModeButton().IsChecked().Value();
        this->QueuePanel().Visibility(m_compactMode ? Visibility::Collapsed : Visibility::Visible);
        this->QueueColumn().Width(GridLength{ m_compactMode ? 0.0 : 280.0 });
        this->StatusText().Text(m_compactMode ? L"Compact theater mode" : L"Queue visible");
    }

    void MainWindow::RootGrid_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args)
    {
        switch (args.Key())
        {
        case Windows::System::VirtualKey::Space:
            PlayPause_Click(nullptr, RoutedEventArgs{ nullptr });
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::Left:
            SeekBy(-5);
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::Right:
            SeekBy(5);
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::Up:
            this->VolumeSlider().Value(std::min(100.0, this->VolumeSlider().Value() + 5.0));
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::Down:
            this->VolumeSlider().Value(std::max(0.0, this->VolumeSlider().Value() - 5.0));
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::M:
            Mute_Click(nullptr, RoutedEventArgs{ nullptr });
            args.Handled(true);
            break;
        case Windows::System::VirtualKey::F:
            FullWindow_Click(nullptr, RoutedEventArgs{ nullptr });
            args.Handled(true);
            break;
        default:
            break;
        }
    }

    void MainWindow::RefreshSubtitleTracks()
    {
        if (!m_currentPlaybackItem)
        {
            return;
        }

        auto tracks = m_currentPlaybackItem.TimedMetadataTracks();
        std::vector<uint32_t> subtitleIndices;
        for (uint32_t index = 0; index < tracks.Size(); ++index)
        {
            auto kind = tracks.GetAt(index).TimedMetadataKind();
            if (kind == TimedMetadataKind::Subtitle || kind == TimedMetadataKind::Caption ||
                kind == TimedMetadataKind::ImageSubtitle)
            {
                subtitleIndices.push_back(index);
            }
        }

        if (subtitleIndices == m_subtitleTrackIndices)
        {
            return;
        }

        auto previousSelection = this->SubtitlePicker().SelectedIndex();
        m_subtitleTrackIndices = std::move(subtitleIndices);
        m_updatingSubtitlePicker = true;
        this->SubtitlePicker().Items().Clear();
        this->SubtitlePicker().Items().Append(box_value(L"Subtitles off"));
        for (uint32_t index = 0; index < m_subtitleTrackIndices.size(); ++index)
        {
            auto track = tracks.GetAt(m_subtitleTrackIndices[index]);
            auto name = track.Name();
            if (name.empty())
            {
                name = L"Subtitle " + to_hstring(index + 1);
            }
            this->SubtitlePicker().Items().Append(box_value(name));
        }
        auto selection = std::clamp(previousSelection, 0,
            static_cast<int32_t>(m_subtitleTrackIndices.size()));
        this->SubtitlePicker().SelectedIndex(selection);
        m_updatingSubtitlePicker = false;
    }

    void MainWindow::SubtitlePicker_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_updatingSubtitlePicker || !m_currentPlaybackItem)
        {
            return;
        }

        auto selected = this->SubtitlePicker().SelectedIndex();
        auto tracks = m_currentPlaybackItem.TimedMetadataTracks();
        for (uint32_t index = 0; index < m_subtitleTrackIndices.size(); ++index)
        {
            auto mode = selected == static_cast<int32_t>(index + 1)
                ? TimedMetadataTrackPresentationMode::PlatformPresented
                : TimedMetadataTrackPresentationMode::Disabled;
            tracks.SetPresentationMode(m_subtitleTrackIndices[index], mode);
        }
        this->StatusText().Text(selected > 0 ? L"Subtitle track enabled" : L"Subtitles off");
    }

    void MainWindow::SeekSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const&)
    {
        if (m_updatingSeekSlider || m_playlist.empty())
        {
            return;
        }

        auto session = m_mediaPlayer.PlaybackSession();
        if (session.CanSeek())
        {
            session.Position(Windows::Foundation::TimeSpan{
                static_cast<int64_t>(this->SeekSlider().Value() * 10'000'000.0) });
        }
    }

    void MainWindow::VolumeSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const&)
    {
        if (m_mediaPlayer)
        {
            m_mediaPlayer.Volume(this->VolumeSlider().Value() / 100.0);
        }
    }

    void MainWindow::AudioBalanceSlider_ValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const&)
    {
        if (m_mediaPlayer)
        {
            m_mediaPlayer.AudioBalance(this->AudioBalanceSlider().Value() / 100.0);
        }
    }

    void MainWindow::SpeedPicker_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_mediaPlayer)
        {
            return;
        }

        constexpr double rates[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
        auto selectedIndex = this->SpeedPicker().SelectedIndex();
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < std::size(rates))
        {
            m_mediaPlayer.PlaybackSession().PlaybackRate(rates[selectedIndex]);
        }
    }

    void MainWindow::UpdatePlaybackState()
    {
        auto isPlaying = m_mediaPlayer &&
            m_mediaPlayer.PlaybackSession().PlaybackState() == MediaPlaybackState::Playing;
        this->PlayPauseButton().Icon(SymbolIcon(isPlaying ? Symbol::Pause : Symbol::Play));
        AutomationProperties::SetName(this->PlayPauseButton(), isPlaying ? L"Pause" : L"Play");
    }

    void MainWindow::UpdateTimeline()
    {
        if (!m_mediaPlayer || m_playlist.empty())
        {
            return;
        }

        auto session = m_mediaPlayer.PlaybackSession();
        auto currentSeconds = session.Position().count() / 10'000'000.0;
        auto durationSeconds = session.NaturalDuration().count() / 10'000'000.0;
        this->CurrentTimeText().Text(FormatTime(currentSeconds));
        this->DurationText().Text(FormatTime(durationSeconds));
        this->SeekSlider().IsEnabled(session.CanSeek() && durationSeconds > 0);

        if (durationSeconds > 0)
        {
            m_updatingSeekSlider = true;
            this->SeekSlider().Maximum(durationSeconds);
            this->SeekSlider().Value(std::clamp(currentSeconds, 0.0, durationSeconds));
            m_updatingSeekSlider = false;
        }
    }
}
