#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <audio_lib.h>
#include <video_lib.h>

#include "player.h"

#include "playback_libeplayer3.h"

extern cAudio *audioDecoder;
extern cVideo *videoDecoder;

//Used by Fileplay
bool cPlayback::Open(playmode_t PlayMode)
{
	const char *aPLAYMODE[] = {
		"PLAYMODE_TS",
		"PLAYMODE_FILE"
	};

	if (PlayMode != PLAYMODE_TS) {
		audioDecoder->closeDevice();
		videoDecoder->closeDevice();
		decoders_closed = true;
	}

	printf("%s:%s - PlayMode=%s\n", __FILE__, __func__, aPLAYMODE[PlayMode]);
	pm = PlayMode;
	fn_ts = "";
	fn_xml = "";
	last_size = 0;
	nPlaybackSpeed = 0;
	init_jump = -1;

	return 0;
}

//Used by Fileplay
void cPlayback::Close(void)
{
	printf("%s:%s\n", __FILE__, __func__);

//Dagobert: movieplayer does not call stop, it calls close ;)
	Stop();
	if (decoders_closed)
	{
		audioDecoder->openDevice();
		videoDecoder->openDevice();
		decoders_closed = false;
	}
}

//Used by Fileplay
bool cPlayback::Start(char *filename, int vpid, int vtype, int apid, int ac3, unsigned int)
{
	bool ret = false;
	bool isHTTP = false;
	no_probe = false;

	printf("%s:%s - filename=%s vpid=%u vtype=%d apid=%u ac3=%d, no_probe=%d\n",
		__FILE__, __func__, filename, vpid, vtype, apid, ac3, no_probe);

	init_jump = -1;

	//try to open file
	if(player && player->Open(filename, no_probe)) {
		if (pm != PLAYMODE_TS) {
			player->output.Open();
			
			SetAPid(apid, 0);
			ret = player->Play();
		}
	}

/* konfetti: in case of upnp playing mp4 often leads to a 
 * paused playback but data is already injected which leads
 * to errors ... 
 * and I don't see any sense of pausing direct after starting
 * with the exception of timeshift. but this should be handled
 * outside this lib or with another function!
 */
	if (pm != PLAYMODE_TS) {
		if ((ret) && (!isHTTP)) {
			//pause playback in case of timeshift
			//FIXME: no picture on tv
			if (!player->Pause()) {
			      ret = false;
			      printf("failed to pause playback\n");
			} else
				playing = true;
		} else
			playing = true;
	}
		
	printf("%s:%s - return=%d\n", __FILE__, __func__, ret);

	fn_ts = std::string(filename);
	if (fn_ts.length() > 3 && fn_ts.rfind(".ts") == fn_ts.length() - 3) {
		fn_xml = fn_ts.substr(0, fn_ts.length() - 3) + ".xml";
		no_probe = true;
	}

	if (pm == PLAYMODE_TS) {
		struct stat s;
		if (!stat(filename, &s))
			last_size = s.st_size;
		if (player) {
			ret = true;
			videoDecoder->Stop(false);
			audioDecoder->Stop();
		}
	}
	return ret;
}

//Used by Fileplay
bool cPlayback::Stop(void)
{
	printf("%s:%s playing %d\n", __FILE__, __func__, playing);
	//if(playing==false) return false;

	player->Stop();
	player->output.Close();
	player->Close();

	playing=false;
	return true;
}

bool cPlayback::SetAPid(int pid, bool /* ac3 */)
{
	return player->SwitchAudio(pid);
}

bool cPlayback::SetSubtitlePid(int pid)
{
	return player->SwitchSubtitle(pid);
}

bool cPlayback::SetTeletextPid(int pid)
{
	return player->SwitchTeletext(pid);
}

bool cPlayback::SetSpeed(int speed)
{
	printf("%s:%s playing %d speed %d\n", __FILE__, __func__, playing, speed);

	if (! decoders_closed)
	{
		audioDecoder->closeDevice();
		videoDecoder->closeDevice();
		decoders_closed = true;
		usleep(500000);
		player->output.Open();
		playing = player->Play();
	}

	if(playing==false) 
	   return false;
	
	int result = 0;
		
	nPlaybackSpeed = speed;

	if (speed > 1) {
		/* direction switch ? */
		if (player->isBackWard) {
			result = player->FastBackward(0);
		}
		result = player->FastForward(speed);
	} else if (speed < 0) {
	/* direction switch ? */
		if (player->isForwarding) {
			result = player->Continue();
		}
		result = player->FastBackward(speed);
	} else if (speed == 0) {
	/* konfetti: hmmm accessing the member isn't very proper */
		if ((player->isForwarding) || (!player->isBackWard))
			player->Pause();
		else {
			player->FastForward(0);
		}
	} else {
		result = player->Continue();
	}

	if (init_jump > -1) {
		SetPosition(init_jump);
		init_jump = -1;
	}
	if (!result) {
		printf("returning false\n");
		return false;
	}
	return true;
}

bool cPlayback::GetSpeed(int &speed) const
{
        speed = nPlaybackSpeed;
	return true;
}

void cPlayback::GetPts(uint64_t &pts)
{
	player->GetPts((int64_t &) pts);
}

// in milliseconds
bool cPlayback::GetPosition(int &position, int &duration)
{
	bool got_duration = false;

	/* hack: if the file is growing (timeshift), then determine its length
	 * by comparing the mtime with the mtime of the xml file */
	if (pm == PLAYMODE_TS) {
		struct stat s;
		if (!stat(fn_ts.c_str(), &s))
		{
			if (! playing || last_size != s.st_size)
			{
				last_size = s.st_size;
				time_t curr_time = s.st_mtime;
				if (!stat(fn_xml.c_str(), &s))
				{
					duration = (curr_time - s.st_mtime) * 1000;
					if (! playing)
						return true;
					got_duration = true;
				}
			}
		}
	}

	if(playing==false) return false;

	if (!player->isPlaying) {
		printf("cPlayback::%s !!!!EOF!!!! < -1\n", __func__);
		position = duration + 1000;
		return false;
	}

	int64_t vpts = 0;
	player->GetPts(vpts);

	if(vpts <= 0) {
		//printf("ERROR: vpts==0");
	} else {
		/* len is in nanoseconds. we have 90 000 pts per second. */
		position = vpts/90;
	}

	if (got_duration)
		return true;

	double length = 0;

	player->GetDuration(length);

	if(length <= 0) {
		duration = duration+1000;
	} else {
		duration = length*1000.0;
	}

	return true;
}

bool cPlayback::SetPosition(int position, bool absolute)
{
	if (playing == false)
	{
		/* the calling sequence is:
		 * Start()       - paused
		 * SetPosition() - which fails if not running
		 * SetSpeed()    - to start playing
		 * so let's remember the initial jump position and later jump to it
		 */
		init_jump = position;
		return false;
	}
	float pos = (position/1000.0);
	player->Seek(pos, absolute);
	return true;
}

void cPlayback::FindAllPids(int *pids, unsigned int *ac3flags, unsigned int *numpids, std::string *language)
{
	unsigned int i = 0;

	std::vector<Track> tracks = player->manager.getAudioTracks();
	for (std::vector<Track>::iterator it = tracks.begin(); it != tracks.end() && i < *numpids; ++it) {
		pids[i] = it->pid;
		ac3flags[i] = it->ac3flags;
		language[i] = it->Name;
		i++;
	}

	*numpids = i;
}

void cPlayback::FindAllSubtitlePids(int *pids, unsigned int *numpids, std::string *language)
{
	unsigned int i = 0;

	std::vector<Track> tracks = player->manager.getSubtitleTracks();
	for (std::vector<Track>::iterator it = tracks.begin(); it != tracks.end() && i < *numpids; ++it) {
		pids[i] = it->pid;
		language[i] = it->Name;
		i++;
	}

	*numpids = i;
}

void cPlayback::FindAllTeletextsubtitlePids(int *pids, unsigned int *numpids, std::string *language, int *mags, int *pages)
{
	unsigned int i = 0;

	std::vector<Track> tracks = player->manager.getTeletextTracks();
	for (std::vector<Track>::iterator it = tracks.begin(); it != tracks.end() && i < *numpids; ++it) {
		if (it->type != 2 && it->type != 5) // return subtitles only
			continue;
		pids[i] = it->pid;
		language[i] = it->Name;
		mags[i] = it->mag;
		pages[i] = it->page;
		i++;
	}

	*numpids = i;
}

int cPlayback::GetFirstTeletextPid(void)
{
	std::vector<Track> tracks = player->manager.getTeletextTracks();
	for (std::vector<Track>::iterator it = tracks.begin(); it != tracks.end(); ++it) {
		if (it->type == 1)
			return it->pid;
	}
	return -1;
}

void cPlayback::GetChapters(std::vector<int> &positions, std::vector<std::string> &titles)
{
	player->GetChapters(positions, titles);
}

void cPlayback::GetMetadata(std::vector<std::string> &keys, std::vector<std::string> &values)
{
	player->input.GetMetadata(keys, values);
}

cPlayback::cPlayback(int num __attribute__((unused)))
{
	playing=false;
	decoders_closed = false;
	player = new Player();
}

cPlayback::~cPlayback()
{
	if(player)
		delete player;
}

void cPlayback::RequestAbort() {
	player->RequestAbort();
	while (player->isPlaying)
		usleep(100000);
}

bool cPlayback::IsPlaying() {
	return player->isPlaying;
	return false;
}

uint64_t cPlayback::GetReadCount() {
	if (player)
		return player->readCount;
	return 0;
}

int cPlayback::GetAPid(void)
{
	return player->GetAudioPid();
}

int cPlayback::GetVPid(void)
{
	return player->GetVideoPid();
}

int cPlayback::GetSubtitlePid(void)
{
	return player->GetSubtitlePid();
}

int cPlayback::GetTeletextPid(void)
{
	return player->GetTeletextPid();
}
