#include <at_engine.h>
#include <sms_pdu.h>
#include <radio_packet.h>
#include <keyboard_decoder.h>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace deck;
static unsigned checks=0;
#define CHECK(...) do { ++checks; if(!(__VA_ARGS__)) { std::cerr<<"Failed at line "<<__LINE__<<": "<<#__VA_ARGS__<<'\n'; std::exit(1); } } while(0)
static void feed(AtEngine& engine,const std::string& data){for(char c:data)engine.feed(c);}
static void atTests() {
    std::vector<std::string> tx,urcs;
    AtEngine engine([&](const std::string& s){tx.push_back(s);},[&](const std::string& s){urcs.push_back(s);});
    AtResult result; unsigned done=0;
    CHECK(engine.enqueue({"AT+CSQ",1000,"",false,[&](const AtResult& r){result=r;++done;}}));
    engine.tick(0); CHECK(tx.back()=="AT+CSQ\r");
    feed(engine,"\r\nRING\r\n+CSQ: 15,99\r\n+CMTI: \"SM\",7\r\nNO CARRIER\r\n");
    CHECK(done==0); CHECK(urcs.size()==3);
    feed(engine,"OK\r\n"); CHECK(done==1 && result.ok && result.lines.size()==1);
    CHECK(result.lines[0]=="+CSQ: 15,99");
    CHECK(engine.enqueue({"AT+CMGS=18",60000,"001122",false,[&](const AtResult& r){result=r;++done;}}));
    engine.tick(2); feed(engine,"\r\n> "); CHECK(tx.back()==std::string("001122\x1a"));
    feed(engine,"\r\n+CMTI: \"SM\",8\r\n+CMGS: 27\r\nOK\r\n");
    CHECK(done==2 && result.ok && result.lines==std::vector<std::string>{"+CMGS: 27"});
    engine.enqueue({"AT+CMGS=18",60000,"001122",false,[&](const AtResult& r){result=r;++done;}});
    engine.tick(3);feed(engine,"\r\n+CMS ERROR: 500\r\n");CHECK(!result.ok && !result.uncertain && done==3);
    engine.enqueue({"ATD+15551234567;",1000,"",true,[&](const AtResult& r){result=r;++done;}});
    engine.tick(5);feed(engine,"BUSY\r\n");CHECK(!result.ok && result.final=="BUSY" && done==4);
    unsigned cancelled=0;
    engine.enqueue({"AT+CMGS=18",1000,"001122",false,[&](const AtResult& r){result=r;++done;}});
    engine.enqueue({"AT+CSQ",1000,"",false,[&](const AtResult& r){CHECK(!r.ok);++cancelled;}});
    engine.tick(0xfffffff0u);feed(engine,"\r\n> ");engine.tick(984);
    CHECK(done==5 && result.uncertain && cancelled==1);
    CHECK(tx.back()==std::string(1,'\x1b'));
    feed(engine,"\r\n+CMGS: 28\r\nOK\r\n");CHECK(done==5);
    engine.tick(2985);CHECK(engine.idle());
    engine.enqueue({"AT",1000,"",false,[&](const AtResult& r){result=r;++done;}});
    engine.tick(2986);feed(engine,std::string(1025,'x'));CHECK(done==6 && result.uncertain);
    feed(engine,"\r\nOK\r\n");CHECK(done==6);
    CHECK(!engine.enqueue({"AT\rATH",1000,"",false,{}}));
    CHECK(registration("+CEREG: 2,1")==1);
    CHECK(registration("+CEREG: 5,\"AF32\",\"1234\",7")==5);
    CHECK(registration("+CEREG: 2")==2);
    CHECK(registration("+CEREG: bad")==-1);
    CHECK(registration("+CEREG: 2,11")==11);
    CHECK(registration("+CEREG: 6")==6);
    CHECK(registration("+CEREG: 7,\"1\",\"1234\",7")==7);
    CHECK(registration("+CEREG: 2,10,\"1234\",\"00123456\",7")==10);
    CHECK(registration("+CEREG: 2,12")==-1);
    CHECK(registration("+CEREG: 9,1")==-1);
    CHECK(registration("+CEREG: 2,")==-1);
    CHECK(registration("+CEREG:")==-1);
    CHECK(csv(" 0, 0,\"T-Mobile US\",7")[2]=="T-Mobile US");
    CHECK(phoneNumber("+15551234567"));CHECK(phoneNumber("911"));
    CHECK(!phoneNumber("+"));CHECK(!phoneNumber("123;ATH"));CHECK(!phoneNumber("1234567890123456"));
}
static void atQueueTests() {
    std::vector<std::string> tx;
    AtEngine engine([&](const std::string& s){tx.push_back(s);},[](const std::string&){});
    unsigned done=0;
    auto completed=[&](const AtResult& r){CHECK(r.ok);++done;};
    CHECK(engine.enqueue({"AT+CSQ",1000,"",false,completed}));
    engine.tick(0);
    CHECK(engine.enqueue({"AT+COPS?",1000,"",false,completed}));
    CHECK(engine.enqueue({"AT+CHUP",1000,"",true,completed},true));
    CHECK(tx==std::vector<std::string>{"AT+CSQ\r"}); // Urgent work never interrupts a UART exchange.
    feed(engine,"OK\r\n");engine.tick(1);
    CHECK(tx.back()=="AT+CHUP\r");
    feed(engine,"OK\r\n");engine.tick(2);
    CHECK(tx.back()=="AT+COPS?\r");
    feed(engine,"OK\r\n");CHECK(done==3 && engine.idle());
    CHECK(!engine.enqueue({"",1000,"",false,{}}));
    for(unsigned i=0;i<16;++i)CHECK(engine.enqueue({"AT",1000,"",false,{}}));
    CHECK(!engine.enqueue({"ATH",1000,"",true,{}},true));
    engine.tick(3); // One active exchange plus sixteen pending commands is the maximum.
    CHECK(engine.enqueue({"ATH",1000,"",true,{}},true));
    CHECK(!engine.enqueue({"AT",1000,"",false,{}}));
}
static void atStreamingTests() {
    std::vector<std::string> urcs,lines;
    AtEngine engine([](const std::string&){},[&](const std::string& s){urcs.push_back(s);});
    AtResult result;unsigned done=0;
    CHECK(engine.enqueue({"AT+CMGL=4",30000,"",false,
        [&](const AtResult& r){result=r;++done;},[&](const std::string& line){lines.push_back(line);}}));
    engine.tick(0);
    feed(engine,"AT+CMGL=4\r\n"); // The echoed command is never delivered as inbox data.
    for(unsigned i=0;i<100;++i) {
        feed(engine,"+CMGL: "+std::to_string(i)+",1,,18\r\n001122\r\n");
        if(i==40)feed(engine,"RING\r\n+CEREG: 5\r\n+CPIN: READY\r\nSMS DONE\r\nPB DONE\r\n");
    }
    feed(engine,std::string(1024,'A')+"\r\n");
    CHECK(done==0 && lines.size()==201 && lines.back().size()==1024);
    CHECK(urcs.size()==5);
    feed(engine,"OK\r\n");CHECK(done==1 && result.ok && result.lines.empty());
    CHECK(lines.front()=="+CMGL: 0,1,,18" && lines[1]=="001122");
    // Only matching registration/PIN queries also receive their URC as response data.
    engine.enqueue({"AT+CPIN?",1000,"",false,[&](const AtResult& r){result=r;}});
    engine.tick(1);feed(engine,"+CREG: 1\r\n+CPIN: READY\r\nOK\r\n");
    CHECK(result.lines==std::vector<std::string>{"+CPIN: READY"});
    engine.enqueue({"AT+CREG?",1000,"",false,[&](const AtResult& r){result=r;}});
    engine.tick(2);feed(engine,"+CEREG: 2,1\r\n+CREG: 2,5\r\nOK\r\n");
    CHECK(result.lines==std::vector<std::string>{"+CREG: 2,5"});
    engine.enqueue({"AT+CSQ",1000,"",false,[&](const AtResult& r){result=r;}});
    engine.tick(3);feed(engine,"+CRING: VOICE\r\nVOICE CALL: BEGIN\r\nVOICE CALL: END: 000017\r\nVOICE CALL END: 000017\r\n+CSQ: 15,99\r\nOK\r\n");
    CHECK(result.ok && result.lines==std::vector<std::string>{"+CSQ: 15,99"});
    CHECK(urcs.back()=="VOICE CALL END: 000017");
}
static void atRecoveryTests() {
    std::vector<std::string> tx;
    AtResult activeResult,queuedResult;
    unsigned activeDone=0,queuedDone=0,recovered=0;
    AtEngine engine([&](const std::string& s){tx.push_back(s);},[&](const std::string& s){
        if(s=="RDY")CHECK(engine.enqueue({"AT",1000,"",false,[&](const AtResult& r){CHECK(r.ok);++recovered;}}));
    });
    auto active=[&](const AtResult& r){activeResult=r;++activeDone;};
    auto queued=[&](const AtResult& r){queuedResult=r;++queuedDone;};
    engine.enqueue({"AT+CMGS=18",1000,"001122",false,active});
    engine.enqueue({"AT+COPS?",1000,"",false,queued});
    engine.tick(100);feed(engine,"\r\n> \r\nRDY\r\n");
    CHECK(activeDone==1 && !activeResult.ok && activeResult.uncertain);
    CHECK(queuedDone==1 && !queuedResult.ok && !queuedResult.uncertain);
    CHECK(!engine.idle());
    feed(engine,"+CMGS: 99\r\nOK\r\n");engine.tick(2099);
    CHECK(tx.size()==2 && recovered==0); // The fresh AT remains queued during quarantine.
    engine.tick(2100);CHECK(tx.back()=="AT\r");
    feed(engine,"OK\r\n");CHECK(recovered==1 && engine.idle());
    CHECK(activeDone==1 && queuedDone==1);

    // Explicit power-cycle reset also clears an unfinished line and cancels work before dispatch.
    feed(engine,"stale partial");
    engine.enqueue({"AT+COPS?",1000,"",false,queued});
    engine.reset("Power cycled");CHECK(queuedDone==2 && !queuedResult.uncertain);
    engine.enqueue({"AT",1000,"",false,active});
    engine.tick(4100);feed(engine,"OK\r\n");
    CHECK(activeDone==2 && activeResult.ok && engine.idle());

    // Both response storage limits fail explicitly and discard queued commands.
    engine.enqueue({"AT+CMGL=4",1000,"",false,active});
    engine.enqueue({"AT+COPS?",1000,"",false,queued});
    engine.tick(4101);
    for(unsigned i=0;i<49;++i)feed(engine,"value\r\n");
    CHECK(activeDone==3 && activeResult.uncertain && activeResult.lines.size()==48);
    CHECK(queuedDone==3);
    feed(engine,"OK\r\n");engine.tick(6101);
    engine.enqueue({"AT",1000,"",false,active});engine.tick(6102);
    for(unsigned i=0;i<9;++i)feed(engine,std::string(1024,'A')+"\r\n");
    CHECK(activeDone==4 && activeResult.uncertain && activeResult.lines.size()==8);
    feed(engine,"OK\r\n");engine.tick(8102);
    engine.enqueue({"ATD+15551234567;",1000,"",true,active});engine.tick(8103);
    feed(engine,"NO DIALTONE\r\n");CHECK(activeDone==5 && !activeResult.ok && !activeResult.uncertain);
}
static void smsTests() {
    SmsSubmit out;
    CHECK(encodeSms("+15551234567","hello",out));
    CHECK(out.hex=="0001000B915155214365F7000005E8329BFD06");
    CHECK(out.tpduBytes==18);
    CHECK(encodeSms("+15551234567",std::string(160,'a'),out));
    std::string maxPayload;
    for(unsigned i=0;i<20;++i)maxPayload+="E170381C0E87C3";
    CHECK(out.hex=="0001000B915155214365F70000A0"+maxPayload && out.tpduBytes==153);
    CHECK(!encodeSms("+15551234567",std::string(161,'a'),out));
    CHECK(encodeSms("+15551234567",std::string(80,'~'),out));
    CHECK(!encodeSms("+15551234567",std::string(81,'~'),out));
    CHECK(!encodeSms("+15551234567","emoji \xf0\x9f\x98\x80",out));
    CHECK(!encodeSms("+15551234567","backtick `",out));
    CHECK(!encodeSms("123\nATH","hello",out));
    CHECK(!encodeSms("123","",out));
    const std::string header="00000B915155214365F70000";
    const std::string timestamp="62106100000000";
    SmsDeliver in;
    std::string pdu=header+timestamp+"05E8329BFD06";
    CHECK(decodeSms(pdu,in));CHECK(in.sender=="+15551234567" && in.text=="hello");
    CHECK(decodeSms(header+timestamp+"029B1E",in));CHECK(in.text=="~");
    CHECK(decodeSms("00000B915155214365F70008"+timestamp+"0600480069754C",in));
    CHECK(in.text=="Hi\xe7\x95\x8c");
    std::string multipart=pdu;multipart[2]='4';CHECK(!decodeSms(multipart,in));
    CHECK(!decodeSms(pdu+"00",in));
    for(size_t n=0;n<pdu.size();++n)CHECK(!decodeSms(pdu.substr(0,n),in));
    CHECK(!decodeSms("00000Z",in));
    CHECK(decodeSms(header+timestamp+"A0"+maxPayload,in));
    CHECK(in.text==std::string(160,'a'));
    CHECK(!decodeSms(header+timestamp+"A1"+maxPayload+"00",in));
    CHECK(decodeSms(header+timestamp+"00",in) && in.text.empty());
    CHECK(!decodeSms(header+timestamp+"011B",in)); // A dangling GSM extension escape has no character.
    CHECK(!decodeSms("00000B915155214365F70008"+timestamp+"02D800",in));
    CHECK(!decodeSms("00000B915155214365F70008"+timestamp+"02DFFF",in));
    CHECK(!decodeSms("00000B915155214365F70008"+timestamp+"0100",in));
    CHECK(!decodeSms("00000B915155214365F70004"+timestamp+"0141",in)); // 8-bit binary is not text.
    CHECK(decodeSms("00000B915155214365F70011"+timestamp+"05E8329BFD06",in));
    CHECK(in.text=="hello"); // General GSM-7 with message class 1.
    CHECK(decodeSms("00000B915155214365F700F3"+timestamp+"05E8329BFD06",in));
    CHECK(in.text=="hello"); // Dedicated GSM-7 message class group.
    CHECK(decodeSms("00000B915155214365F70019"+timestamp+"0400480069",in));
    CHECK(in.text=="Hi"); // UCS-2 with message class 1.
    CHECK(!decodeSms("00000B915155214365F70020"+timestamp+"0141",in)); // Compressed content.
    CHECK(!decodeSms("00000B915155214365F7000C"+timestamp+"0141",in)); // Reserved alphabet.
    CHECK(!decodeSms("00000BD15155214365F70000"+timestamp+"0141",in)); // Alphanumeric sender.
    CHECK(!decodeSms("00000B115155214365F70000"+timestamp+"0141",in)); // Invalid address extension bit.
    CHECK(!decodeSms("00000B915155214365170000"+timestamp+"0141",in)); // Odd address needs F padding.
    CHECK(!decodeSms("FF0000",in)); // Service centre length cannot run past the buffer.
    CHECK(in.sender.empty() && in.text.empty());
    // Exercise every septet packing alignment, all supported ASCII and all extension characters.
    const std::string ascii="@$_ !\"#%&'()*+,-./0123456789:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz^{}\\[~]|\r\n";
    for(size_t n=1;n<=160;++n) {
        std::string text(n,'a');
        CHECK(encodeSms("+15551234567",text,out));
        CHECK(decodeSms(header+timestamp+out.hex.substr(26),in));
        CHECK(in.text==text);
    }
    CHECK(encodeSms("+15551234567",ascii,out));
    CHECK(decodeSms(header+timestamp+out.hex.substr(26),in));
    CHECK(in.text==ascii);
    CHECK(encodeSms("123","A",out));CHECK(out.hex=="000100038121F300000141");
    CHECK(fingerprint("")==2166136261u && fingerprint("hello")==0x4f9f2cabu);
}
static void radioTests() {
    std::vector<uint8_t> bytes;
    CHECK(encodeRadio({0x12345678,0xabcdef01,"Hi"},bytes));
    CHECK(bytes==std::vector<uint8_t>({'D','K',1,0,0x12,0x34,0x56,0x78,0xab,0xcd,0xef,1,2,'H','i'}));
    RadioPacket packet;
    CHECK(decodeRadio(bytes.data(),bytes.size(),packet));
    CHECK(packet.sender==0x12345678 && packet.sequence==0xabcdef01 && packet.text=="Hi");
    for(size_t i=0;i<bytes.size();++i)CHECK(!decodeRadio(bytes.data(),i,packet));
    bytes[3]=1;CHECK(!decodeRadio(bytes.data(),bytes.size(),packet));
    CHECK(!encodeRadio({1,1,std::string(161,'a')},bytes));
    CHECK(!encodeRadio({1,1,"line\nbreak"},bytes));
    CHECK(!encodeRadio({0,1,"Hello"},bytes));
    CHECK(encodeRadio({1,1,std::string(160,'a')},bytes));
    CHECK(decodeRadio(bytes.data(),bytes.size(),packet));
    CHECK(!decodeRadio(nullptr,14,packet));CHECK(packet.sender==0 && packet.text.empty());
    CHECK(!decodeRadio(nullptr,0,packet));
    CHECK(!encodeRadio({1,1,""},bytes));
    CHECK(!encodeRadio({1,1,std::string(1,char(127))},bytes));
    CHECK(!encodeRadio({1,1,std::string(1,char(128))},bytes));
    CHECK(encodeRadio({0xffffffffu,0xffffffffu," ~"},bytes));
    CHECK(decodeRadio(bytes.data(),bytes.size(),packet));
    CHECK(packet.sender==0xffffffffu && packet.sequence==0xffffffffu && packet.text==" ~");
    bytes[12]=0;CHECK(!decodeRadio(bytes.data(),bytes.size(),packet));
    CHECK(encodeRadio({1,1,"A"},bytes));bytes[13]=31;
    CHECK(!decodeRadio(bytes.data(),bytes.size(),packet));
}
static void keyboardTests() {
    KeyboardDecoder keyboard;
    CHECK(keyboard.feed(128+10)=='q');CHECK(keyboard.feed(10)==0);
    CHECK(keyboard.feed(128+33)==' ');
    CHECK(keyboard.feed(128+32)==27);
    CHECK(keyboard.feed(128+11)=='\b');CHECK(keyboard.feed(128+21)=='\r');
    keyboard.feed(128+30);CHECK(keyboard.feed(128+9)=='1');
    CHECK(keyboard.feed(128+34)=='0');keyboard.feed(30);
    CHECK(keyboard.feed(128+9)=='w');
    keyboard.feed(128+35);CHECK(keyboard.feed(128+9)=='W');keyboard.feed(35);
    CHECK(keyboard.feed(128+9)=='w');CHECK(keyboard.feed(255)==0);
    keyboard.feed(128+31);CHECK(keyboard.feed(128+9)=='W');
    keyboard.feed(128+35);keyboard.feed(31);CHECK(keyboard.feed(128+9)=='W');
    keyboard.feed(35);CHECK(keyboard.feed(128+9)=='w');
    keyboard.feed(128+30);keyboard.feed(128+31);CHECK(keyboard.feed(128+9)=='1');
    keyboard.feed(30);keyboard.feed(31);
    keyboard.feed(128+30);keyboard.feed(128+35);
    CHECK(keyboard.feed(128+13)==17);CHECK(keyboard.feed(128+14)==18);
    keyboard.feed(35);CHECK(keyboard.feed(128+13)=='\'');CHECK(keyboard.feed(128+14)==';');
    keyboard.feed(128+31);CHECK(keyboard.feed(128+13)==17);CHECK(keyboard.feed(128+14)==18);
    keyboard.feed(30);keyboard.feed(31);CHECK(keyboard.feed(128+13)=='k');CHECK(keyboard.feed(128+14)=='j');
    CHECK(keyboard.feed(0)==0 && keyboard.feed(128)==0 && keyboard.feed(128+36)==0);
    const char base[29]={'p','o','i','u','y','t','r','e','w','q','\b','l','k','j','h','g','f','d','s','a','\r','$','m','n','b','v','c','x','z'};
    const char alternate[29]={'@','+','-','_',')','(','3','2','1','#','\b','"','\'',';',':','/','6','5','4','*','\r','$','.',',','!','?','9','8','7'};
    for(unsigned n=1;n<=29;++n) {
        CHECK(keyboard.feed(uint8_t(128+n))==base[n-1]);
        CHECK(keyboard.feed(uint8_t(n))==0);
    }
    keyboard.feed(128+30);
    for(unsigned n=1;n<=29;++n)CHECK(keyboard.feed(uint8_t(128+n))==alternate[n-1]);
    keyboard.feed(30);
}
static void malformedInputTests() {
    std::mt19937 random(7); SmsDeliver sms; RadioPacket packet;
    for(unsigned n=0;n<12000;++n) {
        std::vector<uint8_t> bytes(random()%260);
        std::string hex;
        for(auto& b:bytes){b=uint8_t(random());hex+="0123456789ABCDEF"[b>>4];hex+="0123456789ABCDEF"[b&15];}
        decodeSms(hex,sms);decodeRadio(bytes.data(),bytes.size(),packet);
    }
}
int main() {
    atTests();atQueueTests();atStreamingTests();atRecoveryTests();
    smsTests();radioTests();keyboardTests();malformedInputTests();
    std::cout<<checks<<" checks passed; 12000 malformed packet pairs exercised\n";
}
