#include "application.h"
#include "wifi_manager.h"
#include <cassert>
#include <iostream>
#include <set>
struct Result { uint32_t id; bool ok; bool enabled; bool recording; uint32_t owner; };
std::vector<Result> replies;
std::set<uint32_t> disconnected;
RemoteServer& RemoteServer::Get() { static RemoteServer server; return server; }
bool RemoteServer::IsClientConnected(uint32_t client) { return disconnected.count(client) == 0; }
void RemoteServer::Reply(const RemoteEvent& event, bool ok, const char*, bool enabled, bool recording,
                         uint32_t owner, bool) { replies.push_back({event.id,ok,enabled,recording,owner}); }
void send(Application& app, const char* op, uint32_t client=1, uint32_t id=1, uint8_t state=0,
          uint32_t recording=0, std::vector<uint8_t> pcm={}) {
    if (std::string(op) == "disconnect") disconnected.insert(client);
    if (std::string(op) == "enable") disconnected.erase(client);
    RemoteEvent event; event.op=op; event.client=client; event.id=id; event.state=state;
    event.recording=recording; event.pcm=std::move(pcm);
    assert(app.PostRemoteEvent(std::move(event))); app.HandleRemoteEvents();
}
int main() {
    {
        Application app;
        send(app,"fire"); assert(!replies.back().ok); assert(app.car_.states.empty());
        send(app,"move",1,1,1); assert(!replies.back().ok); assert(app.car_.states.empty());
        app.protocol_->open=true; send(app,"enable");
        assert(app.remote_enabled_&&app.remote_owner_==1&&app.audio_service_.external);
        assert(!app.protocol_->open&&!app.remote_reply_allowed_);
        send(app,"enable",2); assert(!replies.back().ok); assert(app.remote_owner_==1);
        for(uint8_t state:{1,2,4,8,0}){send(app,"move",1,2,state);assert(app.car_.states.back()==state);}
        send(app,"move",1,3,1);
        send(app,"fire",2); assert(!replies.back().ok); assert(app.car_.states.back()==1);
        send(app,"fire",1,4); assert(replies.back().ok); assert(app.car_.states.back()==0x11);
        send(app,"disable"); assert(!app.remote_enabled_&&!app.audio_service_.external&&app.toggles==1);
        assert(app.car_.states.back()==0);
    }
    {
        Application app;send(app,"enable");send(app,"start",1,7);
        assert(app.remote_recording_&&app.remote_reply_allowed_&&app.protocol_->starts==1);
        auto generation=app.remote_generation_.load();send(app,"move",1,8,4);
        assert(app.remote_recording_&&app.remote_reply_allowed_&&app.remote_generation_>generation);
        assert(app.listening_mode_==kListeningModeRealtime);
        send(app,"pcm",1,9,0,7,{1,0});assert(replies.back().ok);assert(app.audio_service_.frames.empty());
    }
    {
        Application app;send(app,"enable");send(app,"start",1,11);
        send(app,"pcm",1,12,0,11,{0x00,0x80});
        app.audio_service_.drained=false;auto count=replies.size();send(app,"end",1,13,0,11);
        assert(app.remote_ending_&&replies.size()==count&&app.protocol_->stops==0);
        assert(app.audio_service_.frames.size()==1&&app.audio_service_.frames[0].size()==960);
        assert(app.audio_service_.frames[0][0]==-32768&&app.audio_service_.frames[0][1]==0);
        app.audio_service_.drained=true;app.remote_sender_busy_=true;app.FinishRemoteAudio();
        assert(app.remote_ending_&&app.protocol_->stops==0);
        app.remote_sender_busy_=false;app.FinishRemoteAudio();
        assert(!app.remote_ending_&&app.protocol_->stops==1&&replies.back().id==13&&replies.back().ok);
        assert(app.remote_enabled_&&app.audio_service_.external&&!app.remote_reply_allowed_&&app.toggles==0);
    }
    {
        Application app;send(app,"enable");send(app,"start",1,17);send(app,"end",1,18,0,16);
        assert(!replies.back().ok&&!app.remote_reply_allowed_&&app.protocol_->stops==0);
        send(app,"start",1,19);app.audio_service_.accept=false;
        send(app,"pcm",1,20,0,19,std::vector<uint8_t>(1920,0));
        assert(!replies.back().ok&&!app.remote_recording_&&!app.remote_reply_allowed_);
    }
    {
        Application app;send(app,"enable");send(app,"start",1,21);
        auto states=app.car_.states.size();send(app,"disconnect");
        assert(app.remote_enabled_&&!app.remote_owner_&&!app.remote_reply_allowed_);
        assert(app.car_.states.size()==states&&app.toggles==0);
        send(app,"enable",2);assert(app.remote_owner_==2);send(app,"disable",2);assert(app.toggles==1);
    }
    {
        Application app;WifiManager::GetInstance().connected=false;send(app,"enable");
        send(app,"move",1,25,2);assert(replies.back().ok&&app.car_.states.back()==2);
        send(app,"start");assert(!replies.back().ok);WifiManager::GetInstance().connected=true;
        app.protocol_->open_result=false;send(app,"start");assert(!replies.back().ok&&!app.remote_recording_);
    }
    {
        Application app;
        send(app,"enable");
        for(int i=0;i<8;i++){RemoteEvent e;e.op="status";assert(app.PostRemoteEvent(std::move(e)));}
        RemoteEvent e;e.op="status";assert(!app.PostRemoteEvent(std::move(e)));
        disconnected.insert(1);
        RemoteEvent disconnect;disconnect.op="disconnect";disconnect.client=1;assert(app.PostRemoteEvent(std::move(disconnect)));
        app.HandleRemoteEvents();assert(app.remote_owner_==0&&app.remote_enabled_);
    }
    {
        Application app;send(app,"enable");send(app,"start",1,30);
        app.audio_service_.drained=false;send(app,"end",1,31,0,30);
        assert(app.remote_ending_);const auto count=replies.size();send(app,"move",1,32,0);
        assert(replies.size()==count+1);
        assert(replies.back().id==32&&replies.back().ok&&app.remote_ending_);
    }
    {
        Application app;send(app,"enable");
        // 不调用 HandleRemoteEvents，模拟主任务阻塞在云端握手。
        for(int i=0;i<8;i++){RemoteEvent e;e.op="status";assert(app.PostRemoteEvent(std::move(e)));}
        RemoteEvent stop;stop.op="move";stop.client=1;stop.id=50;stop.state=0;
        assert(app.PostRemoteEvent(std::move(stop)));assert(app.car_.states.back()==0);
        assert(replies.back().id==50&&replies.back().ok);
        RemoteEvent fire;fire.op="fire";fire.client=1;fire.id=52;
        assert(app.PostRemoteEvent(std::move(fire)));assert(app.car_.states.back()==0x10);
        assert(replies.back().id==52&&replies.back().ok);
        RemoteEvent close;close.op="disable";close.client=1;close.id=51;
        assert(app.PostRemoteEvent(std::move(close)));assert(app.car_.states.back()==0);
        app.HandleRemoteEvents();assert(!app.remote_enabled_&&app.toggles==1);
    }
    std::cout << "Remote controller: 9 flow groups passed (ownership, continuous listening, drain-before-stop, tail, invalid frames, disconnect, offline buttons, queue limit, stop during blocked main task)\n";
}
