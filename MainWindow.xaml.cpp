#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <chrono>
#include <commdlg.h>
#include <cwchar>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::Playback;

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
        });
        m_timer.Start();

        this->StatusText().Text(L"Ready when you are");
        this->QueueCountText().Text(L"0 videos");
        this->NowPlayingText().Text(L"Choose a video");
    }

    hstring MainWindow::NormalizeFilePathForUri(std::wstring const& sourcePath)
    {
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
            L"Video files (*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.mpeg;*.mpg;*.3gp;*.asf)\0"
            L"*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.mpeg;*.mpg;*.3gp;*.asf\0"
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

        auto const wasEmpty = m_playlist.empty();
        for (auto const& path : selectedPaths)
        {
            m_playlist.push_back(path);
            auto nameStart = path.find_last_of(L"\\/");
            auto fileName = nameStart == std::wstring::npos ? path : path.substr(nameStart + 1);
            m_playlistItems.Append(hstring(fileName));
        }

        this->QueueCountText().Text(to_hstring(m_playlist.size()) +
            (m_playlist.size() == 1 ? L" video" : L" videos"));
        this->ClearQueueButton().IsEnabled(!m_playlist.empty());
        this->QueueEmptyState().Visibility(Visibility::Collapsed);
        if (wasEmpty && !m_playlist.empty())
        {
            this->PlaylistView().SelectedIndex(0);
        }
        else if (!selectedPaths.empty())
        {
            this->StatusText().Text(to_hstring(selectedPaths.size()) + L" video(s) added to queue");
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
        auto mediaSource = MediaSource::CreateFromUri(Uri(NormalizeFilePathForUri(filePath)));
        m_mediaPlayer.Source(mediaSource);
        this->EmptyState().Visibility(Visibility::Collapsed);
        this->NowPlayingText().Text(hstring(m_playlistItems.GetAt(static_cast<uint32_t>(index))));
        this->StatusText().Text(L"Playing");
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
        this->FullWindowButton().Content().as<FontIcon>().Glyph(fullWindow ? L"\uE73F" : L"\uE740");
    }

    void MainWindow::Mute_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_mediaPlayer.IsMuted(!m_mediaPlayer.IsMuted());
        auto isMuted = m_mediaPlayer.IsMuted();
        this->MuteButton().IsChecked(isMuted);
        this->MuteButton().Content().as<FontIcon>().Glyph(isMuted ? L"\uE74F" : L"\uE767");
    }

    void MainWindow::Repeat_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto isRepeating = this->RepeatButton().IsChecked().Value();
        this->StatusText().Text(isRepeating ? L"Repeating current video" : L"Repeat off");
    }

    void MainWindow::ClearQueue_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_mediaPlayer.Pause();
        m_mediaPlayer.Source(Windows::Media::Playback::IMediaPlaybackSource{ nullptr });
        m_playlist.clear();
        m_playlistItems.Clear();
        m_currentIndex = 0;

        m_updatingPlaylistSelection = true;
        this->PlaylistView().SelectedIndex(-1);
        m_updatingPlaylistSelection = false;

        this->QueueCountText().Text(L"0 videos");
        this->ClearQueueButton().IsEnabled(false);
        this->QueueEmptyState().Visibility(Visibility::Visible);
        this->EmptyState().Visibility(Visibility::Visible);
        this->NowPlayingText().Text(L"Choose a video");
        this->StatusText().Text(L"Queue cleared");
        this->CurrentTimeText().Text(L"00:00");
        this->DurationText().Text(L"00:00");
        this->SeekSlider().Value(0);
        this->SeekSlider().IsEnabled(false);
        UpdatePlaybackState();
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
        this->PlayPauseButton().Content().as<FontIcon>().Glyph(isPlaying ? L"\uE103" : L"\uE102");
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
