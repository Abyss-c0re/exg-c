package com.abysscore.exgc;

public final class ExgNative {
    static {
        System.loadLibrary("exg");
    }

    /** Exists so the class is not instantiated. */
    private ExgNative() {}

    /** Files directory for the host; null or empty leaves the previous root. 0 on success, including a second call, and -1 if the command thread could not be created. */
    public static native int start(String filesDir);
    /** Cache directory. Flash lines are appended to flash.log here. */
    public static native void setTempDir(String path);
    /** Stops the command thread, drops the link, and stops the share API. A second call returns. */
    public static native void shutdown();
    /** UI-thread pump: drains share ops, saves a pending mode, and restarts a stalled USB stream. Returns at once if the host was never brought up. */
    public static native void tick();
    /** Opens the selected port, or the LAN follow. Returns 1 if the link is up afterwards. */
    public static native int connect();
    /** Closes USB or the LAN follow. Returns if a flash is running and flash ownership is clear. */
    public static native void disconnect();
    /** True while USB or a LAN follow is up. Not the same as frames arriving. */
    public static native boolean connected();
    /** Status line, truncated to the host buffer. Empty rather than null. */
    public static native String status();
    /** True when the last status line is a success. False marks a fault. */
    public static native boolean statusOk();
    /** Measured frames per second over the last full second. At most 1 reads as 0. */
    public static native float sps();
    /** Samples seen. A live LAN cursor is used when that link is up; USB uses the ring total. */
    public static native int frames();
    /** Samples the ring overwrote. Not USB packet loss. */
    public static native int drops();
    /** Window of cooked µV for channel ch, 0..7. Returns 0 if ch is out of range, dst is null, or dst is shorter than 8. */
    public static native int copyWave(int ch, float[] dst);
    /** Plot half-scale in µV, as stored. Not clamped here. */
    public static native int scaleUv();
    /** Next of 50, 100, 200, 500, 1000, 2000, 5000 µV, then wraps, and saves. An unknown stored value becomes 200 and is not saved. */
    public static native void cycleScale();
    /** Window length in seconds. A stored value under 1 reads as 2. */
    public static native int windowS();
    /** Next of 1, 2, 4, 8 seconds, and saves. An unknown stored value becomes 2 and is saved. */
    public static native void cycleWindow();
    /** Turns channel ch (0..7) on or off and saves. Refuses to turn off the last channel, and a bad index returns. */
    public static native void setActive(int ch, boolean on);
    /** Bias for one channel, saved. NEG RAIL refuses and leaves bias off; the board command waits until connect if the cable is down. */
    public static native void setRld(int ch, boolean on);
    /** Next legal gain (1, 2, 3, 4, 6, 8, 12) and saves. Sends it only if that channel is connected and on; a bad index returns. */
    public static native void cycleGain(int ch);
    /** True if channel ch (0..7) is on. False if ch is out of range. */
    public static native boolean active(int ch);
    /** True if this channel's bias pin is on. NEG RAIL forces the answer to false. */
    public static native boolean rld(int ch);
    /** True while NEG RAIL is on. Bias stays off and each channel keeps its own minus site. */
    public static native boolean negRail();
    /** Turns it on or off and saves. On forces bias off, CAR off, and minus-site picking on; off does not restore the electrode map. */
    public static native void setNegRail(boolean on);
    /** Restore the eight default pairs. Leaves NEG RAIL as it is. */
    public static native void montageDefault();
    /** 10-10 index of the minus site, or -1 if it is unset or ch is outside 0..7. */
    public static native int negSite(int ch);
    /** Sets the minus site for one channel and saves. -1 clears it; an index outside the 10-10 list returns without a change, and this does not turn NEG RAIL on. */
    public static native void setNegSite(int ch, int site);
    /** Minus-site name, or NONE. Empty rather than null. */
    public static native String negName(int ch);
    /** True if the next site tap assigns a minus site. False assigns the plus site. */
    public static native boolean negPick();
    /** True picks minus sites and false picks plus sites, without saving. Turning it on moves focus to the selected channel's minus site when that site is set. */
    public static native void setNegPick(boolean on);
    /** ADS1299 multiplier: 1, 2, 3, 4, 6, 8, or 12. Out of range reads as 12. */
    public static native int gain(int ch);
    /** Packed 0xAARRGGBB. A missing channel stays 200, 200, 200. */
    public static native int color(int ch);
    /** Next palette color for the channel, and saves. */
    public static native void cycleColor(int ch);
    /** Stores RGB with alpha ignored, each component clamped to 0..255, and saves. A bad channel returns without a change. */
    public static native void setColor(int ch, int rgb);
    /** A positive value is the half-scale in µV and turns autoscale off. 0 or negative turns autoscale on and does not change the stored scale; either way is saved. */
    public static native void setScaleUv(int uv);
    /** Clamps the window to 1..8 seconds and saves. */
    public static native void setWindowS(int s);
    /** Sets 0, 50, 60, or -1 Hz, and anything else becomes 50. Saves and recooks. */
    public static native void setNotch(int hz);
    /** Sets 0 (off), 1, 2, 5, or 20 Hz, and anything else becomes 1. Saves and recooks. */
    public static native void setHp(int hz);
    /** Sets 0 (off), 20, or 40 Hz, and anything else becomes 0. Saves and recooks. */
    public static native void setLp(int hz);
    /** Loads that preset. Out of range becomes RAW (0). */
    public static native void setBand(int band);
    /** Selects that library index and saves. An out-of-range id becomes 0. */
    public static native void setAlgo(int id);
    /** One-line rule for the selected cube bit when any cube exists, otherwise the library selection. A stock source uses its built-in sentence, and a -1 inherit does not climb to the cube algo. */
    public static native String algoRule();
    /** Full source of the library selection, not of a made-cube bit. Empty rather than null. */
    public static native String algoSrc();
    /** Compiles and stores source on the library selection, then saves; null becomes empty. Returns the error text, or empty when the save succeeds. */
    public static native String setAlgoSrc(String src);
    /** Channel bitmask from the live fold, 0 if the board is not connected. Bit 0 is channel 1. */
    public static native int algoFold();
    /** How many algos are in the library, after the built-in eight are seeded. */
    public static native int alibN();
    /** Selected library index, after seeding. */
    public static native int alibSel();
    /** Selects i and also makes it the live algo. Out of range does nothing and does not save. */
    public static native void alibSetSel(int i);
    /** Name at i. Out of range is empty rather than null. */
    public static native String alibName(int i);
    /** Source at i. An index outside the library returns the compare default, never null. */
    public static native String alibSrc(int i);
    /** True if i is a built-in slot, 0..7. Those cannot be renamed or deleted. */
    public static native boolean alibDef(int i);
    /** Compile only, and does not save; null becomes empty. Returns the error text, or empty when the check succeeds. */
    public static native String alibCheck(String src);
    /** Compiles, stores, and writes the ini; null becomes empty. Returns the error text, or empty on success, and a bad index or a failed compile leaves the slot unchanged. */
    public static native String alibSetSrc(int i, String src);
    /** Renames a user algo and saves; null becomes empty. A built-in index, an empty name, or a bad index returns -1, and characters other than letters, digits, '_' and '-' become '_'. */
    public static native int alibSetName(int i, String name);
    /** Appends userN with the default source and saves. Returns the new index, or -1 if the list is already full. */
    public static native int alibAdd();
    /** Removes a user algo and shifts later indexes down, then saves. -1 for a built-in or a bad index, and then nothing moves. */
    public static native int alibDel(int i);
    /** Restores a built-in name and source, and saves. -1 if i is not a built-in. */
    public static native int alibReset(int i);
    /** Effective algo index: the bit, else the cube algo, else 0. Out of range returns the live algo. */
    public static native int madeAlgo(int cube, int q);
    /** The bit's own algo index. -1 means the same as the cube. */
    public static native int madeAlgoOwn(int cube, int q);
    /** Sets the bit's algo and saves. -1 means inherit the cube, and the return is -1 if the cube, the bit, or id (other than -1) is out of range. */
    public static native int madeSetAlgo(int cube, int q, int id);
    /** Common algo on all 8 bits, or -1 if they are mixed. */
    public static native int madeAlgoAll(int cube);
    /** Sets the cube algo, clears every bit to inherit, selects it as the live algo, and saves. id must be a real library index, not -1. */
    public static native int madeSetAlgoAll(int cube, int id);
    /** Stores tenths, saves, and turns anything outside 8..22 into 15. Drawing still treats a value other than 10, 15, or 20 as 15. */
    public static native void setUiScale(int tenths);
    /** Preference only: an IMU frame if imu is true, else plain EXG, not auto. Refuses while connected. */
    public static native void setBoardImu(boolean imu);
    /** Sets the gain only if it is 1, 2, 3, 4, 6, 8, or 12, then saves. Any other value, or a bad channel, returns without a change. */
    public static native void setGain(int ch, int gain);
    /** Starts the desk wait, or the sit-still wait if a noise plate already exists. Returns if a timed phase is already running. */
    public static native void calStart();
    /** 0 idle, 1 desk, 2 wear prompt, 3 sit still, 4 done, 5 put-down. */
    public static native int calPhase();
    /** 0..99 during a timed phase, 100 when done, else 0. Does not finish the phase. */
    public static native int calProgress();
    /** Short prompt for the current phase, with seconds left. Empty rather than null. */
    public static native String calLine();
    /** Same entry as cal start. An older button still lands in the timed plate. */
    public static native void noiseArm();
    /** During the desk phase, captures the noise plate now. Otherwise starts cal. */
    public static native void noiseOk();
    /** If a noise plate is in memory, starts the 8 s sit-still timer again. Otherwise the capture asks for noise first. */
    public static native void calm();
    /** Flips DC/CLEAN and saves. Does not capture a plate. */
    public static native void toggleClean();
    /** True after a noise plate is stored in memory. */
    public static native boolean calHave();
    /** True after a still plate is stored in memory. */
    public static native boolean calmHave();
    /** True when DC subtract is switched on. Not whether the Wiener fit can run. */
    public static native boolean cleanOn();
    /** True only when Wiener CLEAN actually runs, which needs a noise plate and a window at least as long as the FFT. */
    public static native boolean cleanLive();
    /** Copies into the MATCH or take edit buffer and does not save. Null or empty clears it. */
    public static native void setName(String s);
    /** Current MATCH or take name, which may be empty. Never returned as null. */
    public static native String getName();
    /** Starts a 4 s pose capture, or cancels one that is already running. Does not write a take file. */
    public static native void record();
    /** Turns MATCH on or off. Off clears the pose winner and the take winner; on does not record by itself. */
    public static native void toggleMatch();
    /** True while MATCH is naming poses and takes. */
    public static native boolean matchOn();
    /** Stores a legal profile name only. Null or an illegal name is ignored, and no file is loaded or written. */
    public static native void setProfile(String s);
    /** Current profile name, which may be empty. Empty rather than null. */
    public static native String getProfile();
    /** Saves the profile without the map, then exg-c.ini. 0 if the name is legal, even when the write failed, and -1 if it is not. */
    public static native int profSave();
    /** Loads the profile, keeps the electrode map, and recooks from raw. Always returns 0, including when the file is missing. */
    public static native int profLoad();
    /** Deletes the current profile. 0 if the name was cleared, -1 if it is still set. */
    public static native int profDel();
    /** Renames the profile file. Null becomes empty, and 0 is returned only when the live name is now that name. */
    public static native int profRename(String s);
    /** Names after a rescan. An empty directory is a zero-length array. */
    public static native String[] profiles();
    /** One port path per line. Empty, rather than null, when none exist. */
    public static native String ports();
    /** Next port, wrapping. Does nothing on a LAN follow. */
    public static native void cyclePort();
    /** Selects a port by index after a rescan. Clamps into range, and does nothing if the list is empty. */
    public static native void setPortI(int i);
    /** 8³ occupancy, 512 bytes, from SMX plus live EXG at mapped 10-10 sites. Returns if dst is null or shorter than 512. */
    public static native void copyCube(byte[] dst);
    /** EXG RMS in µV, eight floats, same cook as the traces. Inactive channels are 0, and a null or short dst returns without writing. */
    public static native void cookUv(float[] dst);
    /** How many software contrasts to draw. 0 while NEG RAIL is on, because each sample is already the plus site minus the minus site. */
    public static native int pairN();
    /** "A-B" name for contrast i. Empty rather than null, and NEG RAIL is not refused here. */
    public static native String pairLabel(int i);
    /** Writes two channel indexes. Both are -1 while NEG RAIL is on, and a null dst or one shorter than 2 returns without writing. */
    public static native void pairChs(int i, int[] dst);
    /** EXG RMS of A minus B in µV, four floats, same cook as the traces. A site that is off is 0, and a null or short dst returns without writing. */
    public static native void pairUv(float[] dst);
    /** True if the two laterality contrasts are selected instead of eight channels. Does not say whether NEG RAIL is on. */
    public static native boolean pairMode();
    /** True selects the two software contrasts and saves. Does not change NEG RAIL, and the subtraction is still refused while NEG RAIL is on. */
    public static native void setPairMode(boolean on);
    /** Samples of channel A minus B over the window, in µV. Returns 0 if dst is null, shorter than 8, or the contrast is invalid, which includes every contrast while NEG RAIL is on. */
    public static native int copyPair(int p, float[] dst);
    /** True if the last copy of laterality slot p was clipped. False if p is outside those two slots, and the flag is stale until a copy runs. */
    public static native boolean pairClipped(int p);
    /** 50, then 60, off, AUTO, and back to 50. Saves and retunes, and does not reload plates. */
    public static native void cycleNotch();
    /** 0, 1, 2, 5, 20 Hz, then wraps. An unknown current value becomes 1 and does not save. */
    public static native void cycleHp();
    /** Notch setting: -1 AUTO, 0 off, 50 or 60. Not the Hz actually applied. */
    public static native int notch();
    /** Effective notch in Hz, with AUTO following the plate. 0 if idle. */
    public static native int notchEff();
    /** High-pass setting in Hz. 0 is off. */
    public static native int hp();
    /** Low-pass setting in Hz. 0 is off. */
    public static native int lp();
    /** 0, 20, 40 Hz, then wraps. An unknown current value becomes 0 and does not save. */
    public static native void cycleLp();
    /** True only when CAR is switched on and NEG RAIL is off. */
    public static native boolean car();
    /** Flips CAR and saves. NEG RAIL forces it off and does not reload plates. */
    public static native void toggleCar();
    /** True when each plot window is detrended. */
    public static native boolean detrend();
    /** Flips detrend and recooks. Does not rebuild the live IIR poles. */
    public static native void toggleDetrend();
    /** True when the live amplitude follower is on. */
    public static native boolean envelope();
    /** Flips the envelope, rebuilds the poles, and recooks. */
    public static native void toggleEnvelope();
    /** Preset 0 RAW, 1 LINE, 2 EEG, 3 EMG. May disagree with the knobs. */
    public static native int band();
    /** True when the knobs still match the stored preset. False for a custom mix. */
    public static native boolean bandFit();
    /** Next preset, wrapping through all four. Saves and recooks. */
    public static native void cycleBand();
    /** True if the last wave copy of channel ch hit the rail. False outside 0..7, and the flag is stale until a copy runs. */
    public static native boolean clipped(int ch);
    /** Library index of the live selection, after seeding. Out of range reads as 0. */
    public static native int algo();
    /** Selects the next library entry and saves. Returns without a change if the library is empty. */
    public static native void cycleAlgo();
    /** Name of the live algo. Falls back to the built-in name if the slot is empty, and is never null. */
    public static native String algoName();
    /** Flips plot pause. Does not save and does not stop USB. */
    public static native void togglePause();
    /** True while the plot is held. Does not say whether the board is still streaming. */
    public static native boolean paused();
    /** True while a CSV file is open. */
    public static native boolean csvOn();
    /** Opens or closes the timestamped CSV. Same rules as the on-screen record control. */
    public static native void toggleCsv();
    /** Starts a CSV at path, replacing an open one. Null becomes empty, and -1 means disconnected, an empty path, or a file that cannot be created. */
    public static native int csvBegin(String path);
    /** Starts a CSV on an already-open fd. Null name becomes empty; fd is closed on failure, including when not connected, and -1 means a bad fd or a failed fdopen. */
    public static native int csvBeginFd(int fd, String name);
    /* Fills up to 64 bins (128-pt strip). Returns peak Hz. */
    public static native int copyFft(float[] dst);
    /** Starts the take timer, or stops it. Stopping does not save. */
    public static native void toggleAtom();
    /** Starts counting take seconds from 0. Does not write a file until a later save. */
    public static native void atomStart();
    /** Stops the take and returns how many seconds were counted. 0 if none, and no file is written. */
    public static native int atomStop();
    /** True while a take is being counted. */
    public static native boolean atomOn();
    /** Seconds counted in the running take, or 0 if it is not running. */
    public static native int atomN();
    /** Writes the named .npat and raw file and stamps the current NEG RAIL bit. -1 if the name is empty or there are no seconds. */
    public static native int atomSave();
    /** Loads the named .npat as the reference for unity, without raw or a recook. -1 if the name is empty or the file has no seconds. */
    public static native int atomLoad();
    /** Bit agreement of the live ring with the loaded take, from 0 to 1. 0 if either side is empty. */
    public static native float atomUnity();
    /** One status sentence, or empty if nothing is recording and no take is picked. A negative contrast score reads as a different montage, and the text is never null. */
    public static native String atomLine();
    /** Name of the loaded take, or empty. Never null. */
    public static native String atomRef();
    /** Rescans .npat files and returns how many names were kept. */
    public static native int atomCount();
    /** Name at i from the last rescan, which this call does not repeat. Out of range is empty rather than null. */
    public static native String atomAt(int i);
    /** Seconds stored in take i. 0 if i is out of range or the path is bad. */
    public static native int atomSecs(int i);
    /** Copies that take's name and loads it as the reference. -1 if i is out of range; otherwise the load result. */
    public static native int atomSelect(int i);
    /** Deletes the .npat and the raw file, and clears a reference or compare slot that used that name. Out of range returns without deleting. */
    public static native void atomDel(int i);
    /** Stops a running take and drops the second count. Does not delete a saved file. */
    public static native void atomDiscard();
    /** First tap sets compare slot A and loads it; a second tap sets B and scores the two. Tapping A again clears both, and an out-of-range index returns. */
    public static native void atomPick(int i);
    /** Compare sentence for the two slots. With nothing picked, says to tap two takes, and is never null. */
    public static native String atomPair();
    /** Name in compare slot A, or empty. Never null. */
    public static native String atomSlotA();
    /** Name in compare slot B, or empty. Never null. */
    public static native String atomSlotB();
    /** Winning take index while MATCH is on, or -1. Not a pose index. */
    public static native int atomIdBest();
    /** Closeness of take i to the last second, from 0 to 1, or 0 outside 0..31. Wiped to 0 when no take won, and not a percent. */
    public static native float atomIdScore(int i);

    /** 1 for the 10-10 assign map, 0 for the crimson viz. */
    public static native int cubeView();
    /** Nonzero selects the assign map, 0 the viz. Saves. */
    public static native void setCubeView(int map);
    /** Adds yaw and pitch in radians. Pitch is clamped to -0.35..1.20, and this does not save. */
    public static native void cubeSpin(float yaw, float pitch);
    /** Steps zoom by 0.2 in the sign of dir, clamps to 0.70..2.80, and saves. */
    public static native void cubeZoom(int dir);
    /** Zoom factor after clamp, from 0.70 to 2.80. */
    public static native float cubeZoomF();
    /** Sets zoom, clamps it to 0.70..2.80, and saves. */
    public static native void setCubeZoom(float z);
    /** Yaw π, pitch 0.25, zoom 1, so +z and Fp face the camera. Saves, and this is not the startup pose. */
    public static native void cubeFront();
    /** True if the cube window is floating. */
    public static native boolean cubeFloat();
    /** Flips the floating cube window and saves. */
    public static native void toggleCubeFloat();
    /** How many cubes this board can hold, the channel count divided by 8. */
    public static native int madeMax();
    /** How many cubes exist now, from 0 to 4. */
    public static native int madeN();
    /** Selected cube index. May read 0 when none exist. */
    public static native int madeSel();
    /** Selects cube i if it exists. Out of range does nothing, and this does not save. */
    public static native void madeSetSel(int i);
    /** Appends a cube on free channels, bits inheriting the live algo, and saves. Returns the index, or -1 if already at the max, also capped at 4. */
    public static native int madeAdd();
    /** Removes cube i, shifts the rest down, and saves. -1 if i is out of range. */
    public static native int madeDel(int i);
    /** Channel number 1..8 on that bit, or 0 if the bit is empty or the index is out of range. */
    public static native int madeCh(int cube, int q);
    /** Assigns channel ch, where 0 clears the bit, and saves. -1 if out of range or that channel is already on another cube. */
    public static native int madeSetCh(int cube, int q, int ch);
    /** Packed RGB with no alpha. A bad cube index stays the default crimson 255, 20, 40. */
    public static native int madeRgb(int cube);
    /** Stores each component clamped to 0..255 and saves. A bad cube index returns without writing. */
    public static native void madeSetRgb(int cube, int rgb);
    /** Selected cube bit, 0..7. Out of range reads as 0. */
    public static native int madeQSel();
    /** Selects bit q. Outside 0..7 does nothing, and this does not save. */
    public static native void madeSetQSel(int q);
    /** Source for that bit. A bit of -1 does not inherit the cube algo and gets the compare default, never null. */
    public static native String madeSrc(int cube, int q);
    /** Compiles source onto the bit's own algo index; null becomes empty. Returns the error text, or empty on success, and an inherit bit with no slot does not get a private copy. */
    public static native String setMadeSrc(int cube, int q, String src);
    /** Bitmask of the eight cube bits the algos turned on. 0 if the cube index is bad or the board is not connected; bit 0 is cube bit 1, not channel 1. */
    public static native int madeFold(int cube);
    /** Channel index the next site tap writes. Not clamped here. */
    public static native int elecSel();
    /** Selects channel ch. Out of range returns, and focus moves to the minus site if minus-picking is on and that site is set, otherwise the plus site. */
    public static native void setElecSel(int ch);
    /** "N name", or on NEG RAIL with a minus site, "N plus-minus". Out of range is empty rather than null. */
    public static native String elecLabel(int ch);
    /** Plus-site name, or NONE. Out of range is empty rather than null. */
    public static native String elecName(int ch);
    /** World position of the plus site's cube cell, three floats. Returns if xyz is null or shorter than 3, and a bad channel leaves the array unchanged. */
    public static native void elecXyz(int ch, float[] xyz);
    /** 10-10 index the map keys move. Not clamped here. */
    public static native int siteFocus();
    /** Moves focus by dir names, wrapping the 10-10 list. Does not assign the site. */
    public static native void siteStep(int dir);
    /** Assigns the site to the selected channel's minus end if minus-picking is on, otherwise the plus end. -1 clears that end and does not change NEG RAIL. */
    public static native void assignSite(int site);
    /** How many 10-10 names exist. */
    public static native int siteN();
    /** 10-10 name at i, or empty if i is outside the list. Never null. */
    public static native String siteName(int i);
    /** True if this 10-10 name is marked core. False if i is outside the list. */
    public static native boolean siteCore(int i);
    /** Channel 0..7 whose plus site is i, or -1 if none. Does not look at minus sites. */
    public static native int siteCh(int i);
    /** Flat map position, table units divided by 10. Returns if xy is null or shorter than 2, and a bad index writes 0, 0. */
    public static native void siteFlat(int i, float[] xy);
    /** World position of that site's cube cell, three floats. Returns if xyz is null or shorter than 3. */
    public static native void siteXyz(int i, float[] xyz);
    /** Focus site as its name and cube indexes i, j, k. Never null. */
    public static native String siteFocusLabel();
    /** Packed viz cells, at most 40: xyz is 3 floats per cell, size one float, rgba one int. Returns 0 if any array is null, xyz is shorter than 3 per cell, or rgba is short. */
    public static native int vizCells(float[] xyz, float[] size, int[] rgba);
    /** SMX sequence counter. Not a channel fold. */
    public static native int smxSeq();
    /** Latest SMX row as a channel bitmask. 0 if no row has been stored, and bit 0 is channel 1. */
    public static native int smxFold();
    /** Writes a profile-shaped ini with no electrode map and no API block. Null becomes empty, and -1 means an empty path or a file that cannot be opened. */
    public static native int profExport(String path);
    /** Reads a profile file, puts the electrode map back, and writes exg-c.ini. Null becomes empty, and -1 means the file cannot be opened, in which case nothing else changes. */
    public static native int profImport(String path);
    /** ID line for the UI. Before the host is ready the text is "ID —", and it is never null. */
    public static native String idLine();
    /** Milliseconds left in the armed 4 s record. 0 if nothing is armed or the window has already elapsed. */
    public static native int recMs();
    /** Take id line when a take exists. Otherwise empty if MATCH is off, "now —" while unnamed, or "now" plus the pose, and never null. */
    public static native String matchLine();
    /** How many MATCH poses are stored. Not the take count. */
    public static native int learnN();
    /** Pose name at i. Out of range is empty rather than null. */
    public static native String learnName(int i);
    /** Wave-and-RMS blend for pose i. 0 if i is out of range or no pose won by a clear margin, and not a percent. */
    public static native float learnScore(int i);
    /** Jaccard of occupied cube cells, from 0 to 1. 0 if i is out of range or that pose has no cube, and this does not replace the wave score. */
    public static native float learnScoreCube(int i);
    /** Index of the winning pose, or -1 when MATCH is off or nothing won. */
    public static native int learnBest();
    /** Index of the pose selected in the list, or -1 if none. */
    public static native int learnSel();
    /** Selects pose i and copies its name into the edit buffer. Out of range does nothing. */
    public static native void learnSelect(int i);
    /** Deletes pose i, its raw file, and rewrites the learn file. Out of range does nothing, and a take is not deleted. */
    public static native void learnDel(int i);
    /** True when an IMU sample is present. False if this board sent no IMU, and the nine values are not returned. */
    public static native boolean imuOk();
    /** Last IMU sample in ring units: acceleration, gyro, then magnetometer, nine floats. Returns if dst is null or shorter than 9. */
    public static native void imu(float[] dst);
    /** True when the live board is the IMU frame. The preference alone does not count. */
    public static native boolean boardImu();
    /** Auto, then IMU, then EXG, then Auto. Refuses while connected. */
    public static native void cycleBoard();
    /** UI scale in tenths. Stored values outside 8..22 read as 15, and drawing still treats anything other than 10, 15, or 20 as 15. */
    public static native int uiScale();
    /** Steps through 15, then 20, then 10, and saves. A value under 15 jumps straight to 15. */
    public static native void cycleUiScale();

    /** True if the API server is on. Stays false until something turns it on. */
    public static native boolean apiOn();
    /** Turns the server on or off and saves. Refuses to turn on when HTTP, UDP, and TCP are all 0, and a failed bind leaves the flag on. */
    public static native void setApiOn(boolean on);
    /** True if the bind is all interfaces. False if it is loopback. */
    public static native boolean apiLan();
    /** True binds all interfaces, false binds loopback. Saves and rebinds, and does not by itself require a token. */
    public static native void setApiLan(boolean lan);
    /** Publish rate in Hz. A stored value under 1 reads as 125. */
    public static native int apiHz();
    /** Clamps the publish rate to 1..500 Hz, saves, and rebinds. */
    public static native void setApiHz(int hz);
    /** HTTP port. 0 means that listener is off. */
    public static native int apiHttp();
    /** Sets the HTTP port and saves. Outside 0..65535 becomes 8765, a clash with UDP or TCP keeps the old port, and 0 is allowed. */
    public static native void setApiHttp(int port);
    /** UDP port. 0 means that listener is off. */
    public static native int apiUdp();
    /** Sets the UDP port and saves. Outside 0..65535 becomes 8766, and a clash with HTTP or TCP keeps the old port. */
    public static native void setApiUdp(int port);
    /** TCP port for 68-byte EXG1 frames. 0 means that listener is off. */
    public static native int apiTcp();
    /** Sets the TCP port and saves. Outside 0..65535 becomes 8767, and a clash with HTTP or UDP keeps the old port. */
    public static native void setApiTcp(int port);
    /** Shared secret, or empty. Loopback GET does not need one, and the string is never null. */
    public static native String apiToken();
    /** Stores the secret with spaces and newlines removed, saves, and rebinds. Null or empty is legal, and loopback GET still does not check it. */
    public static native void setApiToken(String s);
    /** Extra send address, or empty. Never null. */
    public static native String apiPush();
    /** host:port for an extra send, saved. Null becomes empty and turns the extra send off; a bad address is cleared and still saved. */
    public static native void setApiPush(String s);
    /** One status line from the API server. Never null. */
    public static native String apiLine();

    /** True when the plot is following LAN rather than USB. */
    public static native boolean linkApi();
    /** 0 is USB, 1 is LAN. */
    public static native int linkPath();
    /** Flips USB and LAN. A live board is disconnected either way. */
    public static native void cycleLink();
    /** True selects LAN and false selects USB. A value that is already current returns; otherwise a live board is disconnected and the choice is saved. */
    public static native void setLinkApi(boolean api);
    /** 1 selects LAN and any other value selects USB. A value that is already current returns; otherwise a live board is disconnected and the choice is saved. */
    public static native void setLinkPath(int path);
    /** Local peer name: null or empty becomes "exg", spaces become underscores, and other punctuation is dropped. Does not save. */
    public static native void setSelf(String s);
    /** Follow address, host or host:port, or empty. Never null. */
    public static native String linkDest();
    /** Sets the follow address and saves. Null becomes empty, a bt: prefix is stored as empty, a string that is not host or host:port keeps the old address, and a changed address clears the link token. */
    public static native void setLinkDest(String s);
    /** Token sent when following, or empty. Never null. */
    public static native String linkToken();
    /** Stores the follow token and saves. Null or empty clears it, and this does not check that the peer will accept it. */
    public static native void setLinkToken(String s);

    /** How many remembered follow peers are stored. */
    public static native int followN();
    /** Follow peer name at i, or empty. Never null. */
    public static native String followName(int i);
    /** Follow address at i, or empty. Never null. */
    public static native String followDest(int i);
    /** Copies that peer's address and grant into the live follow and forces LAN on. A blank or bt: address returns without saving, and a board that is still open is not disconnected. */
    public static native void followUse(int i);
    /** Removes follow peer i and writes the peer file. A bad index does not write. */
    public static native void followDel(int i);
    /** How many peers this device has allowed to pull EXG. */
    public static native int allowN();
    /** Allowed peer name at i, or empty. Never null. */
    public static native String allowName(int i);
    /** Removes allow entry i and writes the peer file. A bad index does not write. */
    public static native void allowDel(int i);
    /** Adds or updates a follow peer and writes the peer file and the ini. A null name, dest, or grant becomes empty. */
    public static native void followRemember(String name, String dest, String grant);
    /** Grant stored for that follow name, or empty if the name is unknown. A null name becomes empty, and the result is never null. */
    public static native String followGrant(String name);
    /** True if this grant is on the allow list. False if it is null, empty, or unknown. */
    public static native boolean grantOk(String grant);
    /** Starts a pair ask. Null or empty asks as "exg"; 2 means that name is already allowed, and 1 means the user must answer. */
    public static native int pairBegin(String name);
    /** 0 idle, 1 waiting, 2 allowed, 3 refused. A wait older than 60 s becomes refused and the grant is cleared. */
    public static native int pairState();
    /** Name of the peer in the current ask, or empty. Never null. */
    public static native String pairName();
    /** If a request is waiting, mints a grant, stores the allow, and writes the peer file. The status line says allowed even when nothing was waiting. */
    public static native void pairAccept();
    /** If a request is waiting, the state becomes refused and the grant is cleared. The status line says refused even when nothing was waiting. */
    public static native void pairReject();
    /** Grant from the current ask, or empty. Never null. */
    public static native String pairGrant();
    /** Copies the latest 68-byte EXG1 frame. Returns 0 if dst is null, shorter than 68, or there is no sample. */
    public static native int copyExg1(byte[] dst);
    /** Feeds one 68-byte EXG1 frame into the LAN follow path. Returns -1 if src is null, short, or not an EXG1 frame, and 0 if it was accepted. */
    public static native int feedExg1(byte[] src);
    /** Applies view JSON for filters, scale in µV, window seconds, colors, sites, and active flags. Null or empty returns without a change. */
    public static native void applyCfgJson(String js);
    /** JSON object body without braces: filters, scale in µV, window in seconds, colors, sites, NEG RAIL, and the ID string. Empty rather than null if nothing was written. */
    public static native String viewJson();
    /** Does nothing. The argument is ignored, and no socket is opened or closed. */
    public static native void linkWire(boolean on);
    /** Kit text plus each profile already in the list. Empty, never null, when nothing was written. */
    public static native String kitExport();
    /** Applies the kit, including the electrode map, and writes exg-c.ini. Null becomes empty, and -1 means the text is under 8 bytes or the kit cannot be read. */
    public static native int kitImport(String s);

    /** Preferred frame: auto, EXG, or IMU. Not the detected live board. */
    public static native int boardMode();
    /** Stores the frame preference and saves. Refuses while connected, and an unknown mode returns. */
    public static native void setBoardMode(int mode);
    /** USB mode command for that rate. The connected Knight restarts into it, with no bootloader step. */
    public static native void streamMode(int mode);
    /** Short board label, with the snapped SPS when connected. Never null. */
    public static native String modeLabel();
    /** Held rate in SPS: a snap of 125, 200, 250, or 500, else a measured 100–160, else 125. */
    public static native int designSps();
    /** True when frames arrive but stay under 64% of the design rate, and never when the rate is under 80 SPS. */
    public static native boolean streamCold();
    /** Firmware version this build expects. Not what the board last reported. */
    public static native int fwNeed();
    /** Last stored version. 0 means never saved, not firmware 0. */
    public static native int fwHave();
    /** Version parsed from this boot's banner. 0 if that line has not arrived. */
    public static native int fwSeen();
    /** True when a USB Knight looks older than the version this build expects. False on LAN, while disconnected, or before a banner verdict. */
    public static native boolean fwBehind();
    /** Saved stream mode, 0..2. An out-of-range value reads as 0. */
    public static native int fwMode();
    /** Stores the mode and saves. Does not write the board, and an out-of-range value returns. */
    public static native void setFwMode(int mode);
    /** Preset name for that mode, truncated. Never null. */
    public static native String fwLabel(int mode);
    /** Flasher log text. Empty rather than null when nothing has been copied. */
    public static native String flashLog();
    /** idle, arm, run, ok, or err, then a newline and the banner line. */
    public static native String flashState();
    /** Serial debug log. Empty rather than null when nothing has been copied. */
    public static native String debugLog();
    /** Second tap within 8 s uploads the one knight.hex. Does not write the mode byte. */
    public static native void flashUpload();
    /** Arms a full hex upload of that mode. confirmed true starts immediately and skips the second tap. */
    public static native void flashPreset(int mode, boolean confirmed);
    /** EEPROM byte only. Refuses while the image version is behind, and confirmed true skips the second tap. */
    public static native void flashModeOnly(int mode, boolean confirmed);

    /** Copies the 16-byte client id and the 32-byte token, and arms wind forwarding. Returns -1 if either array is null or the wrong length. Does not log the token. */
    public static native int mindstormSetIdentity(byte[] id16, byte[] token32);
    /** Ball bytes, including the auth challenge. A null array returns. */
    public static native void mindstormRx(byte[] bytes);
    /** Next frame to write, or a zero-length array when the queue is empty. */
    public static native byte[] mindstormTx();
    /** Drops auth, keeps the identity, and queues HELLO. Returns 0, or -1 if the identity is missing or the queue is full. */
    public static native int mindstormHello();
    /** True after AUTH_OK until AUTH_NO, HELLO, or a new identity. */
    public static native boolean mindstormAuthed();
    /** True when a closed window may be sent. Defaults on. A new identity turns it on. */
    public static native boolean mindstormWindEnabled();
    /** Wind frames on or off. The traces stay as they are. */
    public static native void mindstormSetWind(boolean on);
    /** True when the last cooked rate rounded to 125, 250, or 500. */
    public static native boolean mindstormRateOk();
    /** rate not supported, authed, or disconnected. A refused rate is shown ahead of auth. Empty rather than null. */
    public static native String mindstormStatus();
    /** Newest closed window, or null when there is nothing new. The first two bytes are samples per second, little-endian. The rest are little-endian int16 channels. Does not require a ball session. */
    public static native byte[] mindstormWindow();
}
