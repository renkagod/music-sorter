document.addEventListener('DOMContentLoaded', () => {
    // API endpoints
    const API_BASE = 'http://127.0.0.1:8765/api';

    // State
    let candidates = [];
    let currentPairIndex = 0;
    let isPlaying = false;
    let activeChannel = 'a'; // 'a' or 'b'
    let currentPair = null;

    // Dual Audio Objects
    const audioA = new Audio();
    const audioB = new Audio();

    // DOM Elements
    const btnScan = document.getElementById('btn-scan');
    const statusText = document.getElementById('status-text');
    const statusCount = document.getElementById('status-count');
    const progressBar = document.getElementById('progress-bar');
    const logOutput = document.getElementById('log-output');

    const viewEmpty = document.getElementById('view-empty');
    const viewAB = document.getElementById('view-ab');
    const viewComplete = document.getElementById('view-complete');

    const titleA = document.getElementById('title-a');
    const formatA = document.getElementById('format-a');
    const durA = document.getElementById('dur-a');
    const pathA = document.getElementById('path-a');
    const btnKeepA = document.getElementById('btn-keep-a');

    const titleB = document.getElementById('title-b');
    const formatB = document.getElementById('format-b');
    const durB = document.getElementById('dur-b');
    const pathB = document.getElementById('path-b');
    const btnKeepB = document.getElementById('btn-keep-b');

    const cardA = document.getElementById('card-a');
    const cardB = document.getElementById('card-b');

    const simScore = document.getElementById('sim-score');
    const simOffset = document.getElementById('sim-offset');

    const seekSlider = document.getElementById('seek-slider');
    const timeCurrent = document.getElementById('time-current');
    const timeDuration = document.getElementById('time-duration');

    const btnPlayPause = document.getElementById('btn-play-pause');
    const toggleA = document.getElementById('toggle-a');
    const toggleB = document.getElementById('toggle-b');

    // Utility formatting
    function formatTime(secs) {
        if (isNaN(secs)) return "0:00";
        const m = Math.floor(secs / 60);
        const s = Math.floor(secs % 60);
        return `${m}:${s < 10 ? '0' : ''}${s}`;
    }

    // Audio Sync & Hot Swap Engine
    audioA.muted = false;
    audioB.muted = true; // Mute B initially

    audioA.addEventListener('timeupdate', () => {
        if (activeChannel === 'a' && !seekSlider.dataset.dragging) {
            seekSlider.value = (audioA.currentTime / (audioA.duration || 1)) * 100;
            timeCurrent.textContent = formatTime(audioA.currentTime);
            timeDuration.textContent = formatTime(audioA.duration);
        }
    });

    audioB.addEventListener('timeupdate', () => {
        if (activeChannel === 'b' && !seekSlider.dataset.dragging) {
            seekSlider.value = (audioB.currentTime / (audioB.duration || 1)) * 100;
            timeCurrent.textContent = formatTime(audioB.currentTime);
            timeDuration.textContent = formatTime(audioB.duration);
        }
    });

    function setHotSwapChannel(channel) {
        activeChannel = channel;
        if (channel === 'a') {
            audioA.muted = false;
            audioB.muted = true;
            toggleA.classList.add('active');
            toggleB.classList.remove('active');
            cardA.classList.add('active-sound');
            cardB.classList.remove('active-sound');
        } else {
            audioA.muted = true;
            audioB.muted = false;
            toggleB.classList.add('active');
            toggleA.classList.remove('active');
            cardB.classList.add('active-sound');
            cardA.classList.remove('active-sound');
        }
    }

    function playAudio() {
        Promise.all([audioA.play(), audioB.play()]).then(() => {
            isPlaying = true;
            btnPlayPause.textContent = "⏸ Пауза";
        }).catch(err => console.warn("Playback error:", err));
    }

    function pauseAudio() {
        audioA.pause();
        audioB.pause();
        isPlaying = false;
        btnPlayPause.textContent = "▶ Проигрывать";
    }

    function togglePlayPause() {
        if (isPlaying) pauseAudio();
        else playAudio();
    }

    // Seek synchronizer
    seekSlider.addEventListener('mousedown', () => seekSlider.dataset.dragging = "true");
    seekSlider.addEventListener('mouseup', () => seekSlider.dataset.dragging = "false");
    seekSlider.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        if (audioA.duration) audioA.currentTime = (val / 100) * audioA.duration;
        if (audioB.duration) audioB.currentTime = (val / 100) * audioB.duration;
    });

    toggleA.addEventListener('click', () => setHotSwapChannel('a'));
    toggleB.addEventListener('click', () => setHotSwapChannel('b'));
    btnPlayPause.addEventListener('click', togglePlayPause);

    // Keyboard Shortcuts
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
        if (e.code === 'Space') {
            e.preventDefault();
            togglePlayPause();
        } else if (e.code === 'Tab' || e.code === 'KeyS') {
            e.preventDefault();
            setHotSwapChannel(activeChannel === 'a' ? 'b' : 'a');
        } else if (e.code === 'Digit1' || e.code === 'KeyA') {
            e.preventDefault();
            makeDecision('a');
        } else if (e.code === 'Digit2' || e.code === 'KeyB') {
            e.preventDefault();
            makeDecision('b');
        }
    });

    // Load Candidate Pair
    function loadCandidatePair(pair) {
        currentPair = pair;
        pauseAudio();

        titleA.textContent = pair.rel_a.split('\\').pop().replace(/\.[^/.]+$/, "");
        formatA.textContent = pair.ext_a;
        durA.textContent = formatTime(pair.dur_a);
        pathA.textContent = pair.rel_a;

        titleB.textContent = pair.rel_b.split('\\').pop().replace(/\.[^/.]+$/, "");
        formatB.textContent = pair.ext_b;
        durB.textContent = formatTime(pair.dur_b);
        pathB.textContent = pair.rel_b;

        simScore.textContent = `${pair.similarity}%`;
        simOffset.textContent = `${pair.offset} кадров`;

        audioA.src = `${API_BASE}/stream?path=${encodeURIComponent(pair.track_a)}`;
        audioB.src = `${API_BASE}/stream?path=${encodeURIComponent(pair.track_b)}`;

        setHotSwapChannel('a');
    }

    // Make Decision
    function makeDecision(keepChoice) {
        if (!currentPair) return;

        fetch(`${API_BASE}/decision`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                pair_id: currentPair.id,
                keep_track: keepChoice,
                track_a_path: currentPair.track_a,
                track_b_path: currentPair.track_b
            })
        }).then(r => r.json()).then(data => {
            candidates.shift();
            statusCount.textContent = `Кандидатов: ${candidates.length}`;

            if (candidates.length > 0) {
                loadCandidatePair(candidates[0]);
            } else {
                showView('complete');
            }
        });
    }

    btnKeepA.addEventListener('click', () => makeDecision('a'));
    btnKeepB.addEventListener('click', () => makeDecision('b'));

    // Views
    function showView(viewName) {
        viewEmpty.classList.add('hidden');
        viewAB.classList.add('hidden');
        viewComplete.classList.add('hidden');

        if (viewName === 'empty') viewEmpty.classList.remove('hidden');
        else if (viewName === 'ab') viewAB.classList.remove('hidden');
        else if (viewName === 'complete') viewComplete.classList.remove('hidden');
    }

    // Trigger Scan
    btnScan.addEventListener('click', () => {
        statusText.textContent = "Статус: Выполняется сканирование...";
        progressBar.style.width = "10%";

        fetch(`${API_BASE}/scan`, { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                candidates = data.ab_candidates || [];
                statusCount.textContent = `Кандидатов: ${candidates.length}`;
                progressBar.style.width = "100%";
                statusText.textContent = "Статус: Сканирование завершено";
                logOutput.textContent = (data.logs || []).join('\n');

                if (candidates.length > 0) {
                    showView('ab');
                    loadCandidatePair(candidates[0]);
                } else {
                    showView('complete');
                }
            })
            .catch(err => {
                statusText.textContent = "Ошибка при сканировании";
                console.error(err);
            });
    });

    // Check initial status
    fetch(`${API_BASE}/status`).then(r => r.json()).then(data => {
        candidates = data.ab_candidates || [];
        statusCount.textContent = `Кандидатов: ${candidates.length}`;
        if (candidates.length > 0) {
            showView('ab');
            loadCandidatePair(candidates[0]);
        }
    });
});
