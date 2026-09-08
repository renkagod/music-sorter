import { create } from "zustand";
import {
  AlbumGroup,
  AudioStatus,
  Candidate,
  DatabaseStats,
  DatabaseTrack,
  DuplicatePair,
  MirrorProgress,
  Settings,
  SystemStats,
  TagScanProgress,
  TrackItem,
} from "./types";

const API_BASE = "http://127.0.0.1:8765/api";

interface AppState {
  activeTab: "duplicates" | "inspector" | "mirror" | "database" | "settings";
  setActiveTab: (tab: "duplicates" | "inspector" | "mirror" | "database" | "settings") => void;

  coreConnected: boolean;
  systemStats: SystemStats;
  checkStatus: () => Promise<void>;

  // Duplicates State
  duplicatePairs: DuplicatePair[];
  activePairIndex: number;
  audioStatus: AudioStatus;
  waveformA: number[];
  waveformB: number[];
  fetchDuplicates: () => Promise<void>;
  startDuplicateScan: () => Promise<void>;
  resolveDuplicate: (index: number, action: "keep_a" | "keep_b" | "skip") => Promise<void>;
  setActivePairIndex: (index: number) => void;
  loadWaveforms: (pair: DuplicatePair) => Promise<void>;

  // Audio Controls
  audioPlay: () => Promise<void>;
  audioPause: () => Promise<void>;
  audioToggle: () => Promise<void>;
  audioSwitchChannel: (channel: "a" | "b") => Promise<void>;
  audioSeek: (percent: number) => Promise<void>;
  audioSetVolume: (volume: number) => Promise<void>;
  pollAudioStatus: () => Promise<void>;

  // Tag Inspector State
  albums: AlbumGroup[];
  selectedAlbumKey: string | null;
  filterTier: string;
  searchQuery: string;
  tagProgress: TagScanProgress;
  setSelectedAlbumKey: (key: string | null) => void;
  setFilterTier: (tier: string) => void;
  setSearchQuery: (query: string) => void;
  startTagScan: () => Promise<void>;
  fetchTagProgress: () => Promise<void>;
  fetchAlbums: () => Promise<void>;
  approveTrack: (trackIndex: number) => Promise<void>;
  approveAlbum: (referenceIndex: number) => Promise<void>;
  skipItem: (trackIndex: number, isAlbum: boolean) => Promise<void>;
  selectCandidate: (trackIndex: number, candidateIndex: number, applyToAlbum: boolean) => Promise<void>;
  updateTrackDetails: (trackIndex: number, title: string, artist: string, trackNo: string, lyrics: string) => Promise<void>;
  setCoverChoice: (trackIndex: number, choice: number, applyToAlbum: boolean) => Promise<void>;
  manualFetchMetadata: (source: string, query: string, referenceIndex: number, applyToAlbum: boolean) => Promise<void>;

  // Mirroring State
  mirrorProgress: MirrorProgress;
  startMirroring: () => Promise<void>;
  fetchMirrorStatus: () => Promise<void>;

  // Database State
  databaseTracks: DatabaseTrack[];
  databaseStats: DatabaseStats;
  dbFilterStatus: number;
  dbFilterFormat: number;
  dbSearchQuery: string;
  setDbFilterStatus: (status: number) => void;
  setDbFilterFormat: (format: number) => void;
  setDbSearchQuery: (query: string) => void;
  queryDatabase: () => Promise<void>;
  syncDatabase: () => Promise<void>;
  exportDatabase: (path?: string) => Promise<void>;

  // Settings State
  settings: Settings;
  fetchSettings: () => Promise<void>;
  saveSettings: (settings: Settings) => Promise<void>;

  // Logs State
  logs: string[];
  isLogsOpen: boolean;
  toggleLogs: () => void;
  fetchLogs: () => Promise<void>;
}

export const useAppStore = create<AppState>((set, get) => ({
  activeTab: "duplicates",
  setActiveTab: (tab) => set({ activeTab: tab }),

  coreConnected: false,
  systemStats: {
    duplicatesCount: 0,
    unresolvedTagsCount: 0,
    totalTracks: 0,
    downloadedTracks: 0,
    missingTracks: 0,
    isTagScanning: false,
    isDuplicateScanning: false,
    isMirroring: false,
  },

  checkStatus: async () => {
    try {
      const res = await fetch(`${API_BASE}/status`);
      if (res.ok) {
        const data = await res.json();
        set({
          coreConnected: true,
          systemStats: data.stats,
        });
      } else {
        set({ coreConnected: false });
      }
    } catch {
      set({ coreConnected: false });
    }
  },

  // -----------------------------------------------------------------------------------------------
  // Duplicates & Audio Deck
  // -----------------------------------------------------------------------------------------------
  duplicatePairs: [],
  activePairIndex: 0,
  audioStatus: {
    isPlaying: false,
    activeChannel: "a",
    currentTime: 0,
    duration: 0,
    volume: 0.5,
  },
  waveformA: [],
  waveformB: [],

  fetchDuplicates: async () => {
    try {
      const res = await fetch(`${API_BASE}/duplicates`);
      if (res.ok) {
        const list: DuplicatePair[] = await res.json();
        set({ duplicatePairs: list });
        if (list.length > 0) {
          const pair = list[get().activePairIndex] || list[0];
          get().loadWaveforms(pair);
        }
      }
    } catch (e) {
      console.error(e);
    }
  },

  loadWaveforms: async (pair: DuplicatePair) => {
    try {
      const [resA, resB] = await Promise.all([
        fetch(`${API_BASE}/audio/waveform?path=${encodeURIComponent(pair.trackA_path)}`),
        fetch(`${API_BASE}/audio/waveform?path=${encodeURIComponent(pair.trackB_path)}`),
      ]);
      if (resA.ok && resB.ok) {
        const wfA = await resA.json();
        const wfB = await resB.json();
        set({ waveformA: wfA, waveformB: wfB });
      }
    } catch (e) {
      console.error(e);
    }
  },

  startDuplicateScan: async () => {
    try {
      await fetch(`${API_BASE}/duplicates/scan`, { method: "POST" });
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  resolveDuplicate: async (index, action) => {
    try {
      const res = await fetch(`${API_BASE}/duplicates/resolve`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ index, action }),
      });
      if (res.ok) {
        await get().fetchDuplicates();
        get().checkStatus();
      }
    } catch (e) {
      console.error(e);
    }
  },

  setActivePairIndex: (index) => {
    const list = get().duplicatePairs;
    if (index >= 0 && index < list.length) {
      set({ activePairIndex: index });
      get().loadWaveforms(list[index]);
      fetch(`${API_BASE}/audio/load`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          pathA: list[index].trackA_path,
          pathB: list[index].trackB_path,
        }),
      });
    }
  },

  audioPlay: async () => {
    await fetch(`${API_BASE}/audio/play`, { method: "POST" });
    get().pollAudioStatus();
  },

  audioPause: async () => {
    await fetch(`${API_BASE}/audio/pause`, { method: "POST" });
    get().pollAudioStatus();
  },

  audioToggle: async () => {
    await fetch(`${API_BASE}/audio/toggle`, { method: "POST" });
    get().pollAudioStatus();
  },

  audioSwitchChannel: async (channel) => {
    await fetch(`${API_BASE}/audio/switch-channel`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ channel }),
    });
    get().pollAudioStatus();
  },

  audioSeek: async (percent) => {
    await fetch(`${API_BASE}/audio/seek`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ percent }),
    });
    get().pollAudioStatus();
  },

  audioSetVolume: async (volume) => {
    await fetch(`${API_BASE}/audio/volume`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ volume }),
    });
    get().pollAudioStatus();
  },

  pollAudioStatus: async () => {
    try {
      const res = await fetch(`${API_BASE}/audio/status`);
      if (res.ok) {
        const data = await res.json();
        set({ audioStatus: data });
      }
    } catch {}
  },

  // -----------------------------------------------------------------------------------------------
  // Tag Inspector
  // -----------------------------------------------------------------------------------------------
  albums: [],
  selectedAlbumKey: null,
  filterTier: "all",
  searchQuery: "",
  tagProgress: {
    isScanning: false,
    done: 0,
    total: 0,
    fraction: 0,
    speed: 0,
    eta: "00:00",
    elapsed: "00:00",
  },

  setSelectedAlbumKey: (key) => set({ selectedAlbumKey: key }),
  setFilterTier: (tier) => set({ filterTier: tier }),
  setSearchQuery: (query) => set({ searchQuery: query }),

  startTagScan: async () => {
    try {
      await fetch(`${API_BASE}/tags/scan`, { method: "POST" });
      get().fetchTagProgress();
    } catch (e) {
      console.error(e);
    }
  },

  fetchTagProgress: async () => {
    try {
      const res = await fetch(`${API_BASE}/tags/progress`);
      if (res.ok) {
        const prog = await res.json();
        set({ tagProgress: prog });
      }
    } catch {}
  },

  fetchAlbums: async () => {
    try {
      const res = await fetch(`${API_BASE}/tags/albums`);
      if (res.ok) {
        const list: AlbumGroup[] = await res.json();
        set({ albums: list });
        if (list.length > 0 && !get().selectedAlbumKey) {
          set({ selectedAlbumKey: list[0].albumKey });
        }
      }
    } catch (e) {
      console.error(e);
    }
  },

  approveTrack: async (trackIndex) => {
    try {
      await fetch(`${API_BASE}/tags/approve-track`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackIndex }),
      });
      await get().fetchAlbums();
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  approveAlbum: async (referenceIndex) => {
    try {
      await fetch(`${API_BASE}/tags/approve-album`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ referenceIndex }),
      });
      await get().fetchAlbums();
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  skipItem: async (trackIndex, isAlbum) => {
    try {
      await fetch(`${API_BASE}/tags/skip`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackIndex, isAlbum }),
      });
      await get().fetchAlbums();
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  selectCandidate: async (trackIndex, candidateIndex, applyToAlbum) => {
    try {
      await fetch(`${API_BASE}/tags/select-candidate`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackIndex, candidateIndex, applyToAlbum }),
      });
      await get().fetchAlbums();
    } catch (e) {
      console.error(e);
    }
  },

  updateTrackDetails: async (trackIndex, title, artist, trackNo, lyrics) => {
    try {
      await fetch(`${API_BASE}/tags/update-track`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackIndex, title, artist, trackNo, lyrics }),
      });
      await get().fetchAlbums();
    } catch (e) {
      console.error(e);
    }
  },

  setCoverChoice: async (trackIndex, choice, applyToAlbum) => {
    try {
      await fetch(`${API_BASE}/tags/cover-choice`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ trackIndex, choice, applyToAlbum }),
      });
      await get().fetchAlbums();
    } catch (e) {
      console.error(e);
    }
  },

  manualFetchMetadata: async (source, query, referenceIndex, applyToAlbum) => {
    try {
      await fetch(`${API_BASE}/tags/manual-fetch`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source, query, referenceIndex, applyToAlbum }),
      });
      setTimeout(() => get().fetchAlbums(), 2500);
    } catch (e) {
      console.error(e);
    }
  },

  // -----------------------------------------------------------------------------------------------
  // Collection Mirroring
  // -----------------------------------------------------------------------------------------------
  mirrorProgress: {
    isRunning: false,
    completedTasks: 0,
    totalTasks: 0,
    createdFolders: 0,
    copiedFallbacks: 0,
    message: "",
  },

  startMirroring: async () => {
    try {
      await fetch(`${API_BASE}/mirror/start`, { method: "POST" });
      get().fetchMirrorStatus();
    } catch (e) {
      console.error(e);
    }
  },

  fetchMirrorStatus: async () => {
    try {
      const res = await fetch(`${API_BASE}/mirror/status`);
      if (res.ok) {
        const data = await res.json();
        set({ mirrorProgress: data });
      }
    } catch {}
  },

  // -----------------------------------------------------------------------------------------------
  // Database Operations
  // -----------------------------------------------------------------------------------------------
  databaseTracks: [],
  databaseStats: {
    totalTracks: 0,
    downloadedTracks: 0,
    missingTracks: 0,
  },
  dbFilterStatus: -1,
  dbFilterFormat: -1,
  dbSearchQuery: "",

  setDbFilterStatus: (status) => set({ dbFilterStatus: status }),
  setDbFilterFormat: (format) => set({ dbFilterFormat: format }),
  setDbSearchQuery: (query) => set({ dbSearchQuery: query }),

  queryDatabase: async () => {
    try {
      const { dbFilterStatus, dbFilterFormat, dbSearchQuery } = get();
      const params = new URLSearchParams();
      if (dbFilterStatus !== -1) params.append("status", dbFilterStatus.toString());
      if (dbFilterFormat !== -1) params.append("format", dbFilterFormat.toString());
      if (dbSearchQuery) params.append("query", dbSearchQuery);

      const [trRes, stRes] = await Promise.all([
        fetch(`${API_BASE}/database/tracks?${params.toString()}`),
        fetch(`${API_BASE}/database/stats`),
      ]);

      if (trRes.ok) {
        const list = await trRes.json();
        set({ databaseTracks: list });
      }
      if (stRes.ok) {
        const stats = await stRes.json();
        set({ databaseStats: stats });
      }
    } catch (e) {
      console.error(e);
    }
  },

  syncDatabase: async () => {
    try {
      await fetch(`${API_BASE}/database/sync`, { method: "POST" });
      await get().queryDatabase();
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  exportDatabase: async (path) => {
    try {
      await fetch(`${API_BASE}/database/export`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ path: path || "" }),
      });
    } catch (e) {
      console.error(e);
    }
  },

  // -----------------------------------------------------------------------------------------------
  // Settings Operations
  // -----------------------------------------------------------------------------------------------
  settings: {
    toSortDir: "",
    outputDir: "",
    flacDir: "",
    mp3Dir: "",
    acoustIdKey: "",
    discogsToken: "",
  },

  fetchSettings: async () => {
    try {
      const res = await fetch(`${API_BASE}/settings`);
      if (res.ok) {
        const cfg = await res.json();
        set({ settings: cfg });
      }
    } catch (e) {
      console.error(e);
    }
  },

  saveSettings: async (settings) => {
    try {
      await fetch(`${API_BASE}/settings`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(settings),
      });
      set({ settings });
      get().checkStatus();
    } catch (e) {
      console.error(e);
    }
  },

  // -----------------------------------------------------------------------------------------------
  // Logs Operations
  // -----------------------------------------------------------------------------------------------
  logs: [],
  isLogsOpen: false,

  toggleLogs: () => set((s) => ({ isLogsOpen: !s.isLogsOpen })),

  fetchLogs: async () => {
    try {
      const res = await fetch(`${API_BASE}/logs`);
      if (res.ok) {
        const l = await res.json();
        set({ logs: l });
      }
    } catch {}
  },
}));
