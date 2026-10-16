# AI chat

Handheld OS can chat with your own **OpenAI-compatible Chat Completions API over Wi-Fi or LTE**. The default API base is `https://api.openai.com/v1` and the default model is `gpt-5.6-luna`. You can change both for another compatible provider. This feature needs your provider's API access and may incur its usage charges; a ChatGPT subscription is not an API key.

## Set up on the device

1. Connect Wi-Fi in **Settings → Connections → Wi-Fi**, or use an activated SIM with working LTE data service and the correct APN.
2. Open **Settings → Messaging → AI** for AI setup. You can also tap **…** in the AI conversation.
3. **E** edits the HTTPS API base URL. Keep the default for OpenAI. A full URL ending in `/chat/completions` is also accepted.
4. **M** opens the model picker. Choose GPT-5.6 Luna, GPT-5.6 Terra, GPT-5.6 Sol or GPT-5.4 Mini, or select Custom to enter another exact model ID. A preset does not grant access to a model; your API account must allow it.
5. **K** enters a replacement API key. Type it on the device, then press Enter to save. The display shows asterisks. Existing keys are never displayed or prefilled. Press Sym to cancel without changing the saved key. Enter on an empty key field removes the key; **X** on AI setup also removes it.
6. Press **A** to open the AI conversation and **start typing in the message field**. Press **Enter** or tap **Send** to send the question and recent AI conversation to the configured provider. Opening a conversation or tapping its message history never sends anything.

Use the printed keyboard symbols with Alt and Shift as needed. API keys can contain up to 255 printable characters without spaces, model IDs up to 95, and base URLs up to 191. A custom provider that requires no bearer token may use a blank key. OpenAI requires an API key. No API request is sent when the device starts, when setup is saved, or when the chat app opens.

## Paste a key over USB

The optional [browser AI setup form](https://ethanpierce.github.io/tdeck-pro-fw/ai-setup.html) lets you paste a long key from your computer. It communicates directly with the connected device over USB; the form does not send your key to a web server. Use Chrome or Edge and close any other serial monitor first. Saving setup does not make an API request. Keep USB consoles/session recordings private.

For manual USB setup, use a 115200-baud serial terminal with newline endings:

```text
ai endpoint https://api.openai.com/v1
ai model gpt-5.6-luna
ai key YOUR_API_KEY
ai settings
```

Replace `YOUR_API_KEY` locally; never post it in an issue or chat. The firmware replies only `AI endpoint saved`, `AI model saved`, or `AI key saved`, without echoing values. `ai clear-key` removes the saved key. `ai` opens the chat app and `ai clear` clears its RAM conversation. Both USB and physical entry validate and save the same isolated settings record. USB key updates turn off raw keyboard tracing too.

For a bench test, `ai ask Explain rainbows briefly` opens the AI thread and sends **one real provider request** using the saved endpoint/model/key and the same composer path. It can incur API usage. It prefers connected Wi-Fi; turn Wi-Fi off first to force LTE. Nothing is sent automatically or retried. Prompts typed into a serial console may be recorded by that console; keep private messages out of shared logs.

## Conversations and controls

Tap **Messages** on Home for the conversation list, then **AI Assistant**.
SMS conversations are listed by phone number, alongside a shared Meshtastic
channel conversation. **New** starts a new SMS draft. J/K and Enter remain
available for keyboard navigation.

Inside the AI conversation:

- **Type immediately** in the bottom message field, up to 384 characters. Ordinary letters, including N/J/K/C, are message text.
- **Enter / Send:** submit the current nonempty message.
- **Older / Newer:** tap to read earlier or later history. On the physical keyboard, hold **Shift + Alt** and press **K/J** for older/newer.
- **…:** tap to open AI settings. **Settings → Messaging → AI** remains available from Home.
- **Sym / <:** return to the list or Home, depending on how the conversation was opened. A submitted request keeps running; Back does not cancel or undo it.

In AI settings, **M** opens the four model presets plus Custom. Use **J/K + Enter** or tap a model. Changing a model keeps the saved key and clears the local AI conversation. **C** clears the conversation once the current request finishes. The connection line shows the current data path and whether a key is saved; request progress and errors remain below the composer.

Touch actions are ignored while the e-paper screen is refreshing. Wait for the updated screen before tapping again.

The app retains up to four completed question/reply pairs in RAM. Later questions include those recent turns. Every request begins with a fixed system prompt describing the small 240×320 e-paper screen and asking for concise plain text, at most **500 characters including spaces and line breaks**, without tables or long lists. The firmware also enforces the 500-character limit: it counts Unicode code points and truncates only at a complete UTF-8 boundary, keeping a separate 2,048-byte safety cap. Combining marks and parts of joined emoji each count separately. The HTTP response is capped at 8 KiB. A shortened reply is identified on screen.

Chat is cleared by restarting, clearing the conversation, or saving AI setup. Changing the endpoint also removes the old API key, so it cannot be forwarded to another provider; add the new provider's key afterward. It is separate from SMS/LoRa message history. Clearing locally does not delete data held by your API provider.

UTF-8 text is retained in RAM, while characters outside the built-in e-paper font appear as `?`. This preview supports text responses only. It does not stream, execute model tools, read SMS/contacts, or share other device history with the provider.

## Connection and credential handling

Wi-Fi is preferred when connected. Otherwise a new request uses LTE when the cellular data service is ready. The chosen path remains fixed for that request: the firmware does not switch paths and resend after a failure. To test LTE, turn Wi-Fi off in **Settings → Connections → Wi-Fi**, then return to the AI conversation. Cellular data requires suitable coverage, a compatible activated SIM/plan and a working APN. Carrier data charges may apply.

Only **HTTPS** endpoints are accepted. On both paths the ESP32 verifies the server's certificate chain against the pinned SDK's bundled public CA roots and checks the hostname. The LTE implementation runs TLS on the ESP32 over the modem's TCP socket; the modem carries encrypted TLS records, rather than receiving the API key as a modem HTTP command. Redirects are refused. Self-signed/local HTTP endpoints are not supported.

Certificate checks need a valid clock. An explicit Wi-Fi request can synchronize it using NTP. After a cold boot without Wi-Fi, the LTE path obtains the modem's network clock before starting TLS. If the clock or certificate cannot be verified, the request fails. The LTE request has a two-minute timeout budget. No provider request or clock-setup request is started by simply opening the app or saving its settings.

The header has a separate three-bar Wi-Fi indicator beside cellular signal and battery. **WiFi** means connected, **W…** connecting, **W--** disconnected and **Woff** off. Wi-Fi strength is sampled periodically; the display refreshes when its bar level changes.

The key is saved in its own `deckai` NVS namespace. It is **not encrypted at rest**: someone with physical flash access or a full-device backup can recover it. Use a provider key with appropriate spending controls and revoke it if the device or a backup is shared or lost. The key is hidden in the UI, unavailable through USB getters or the BLE companion service, and not written to application logs. Keyboard event tracing is turned off when opening the key-entry field. Keep backups private.

A request runs in a separate worker so keyboard input and the UI remain responsive. LTE requests share the modem with other cellular work. Only one AI request runs at a time. Network/TLS timeouts can leave the provider's result uncertain: the firmware **does not automatically retry**. A later, different question includes only completed turns; it does not silently replay a failed prompt. Manually submitting the same question again may create a second billable request.

Common messages:

| Message | Action |
| --- | --- |
| Connect Wi-Fi or wait for LTE data service | Join Wi-Fi in Settings → Connections → Wi-Fi, or check the SIM, APN and cellular registration |
| Add your API key / set endpoint and model | Complete Settings → Messaging → AI |
| Credentials or model access rejected (401/403) | Check the key, provider and permitted model |
| API limit or credit balance reached (429) | Check the provider account's quota or billing |
| Redirect refused | Enter the provider's final HTTPS API address |
| Clock sync / certificate / network timeout | Check Internet access, network time and the endpoint's certificate; retry manually if appropriate |
| Response exceeded 8 KB / no supported text | Choose a provider/model returning ordinary non-streamed text Chat Completions |
| Not enough memory for HTTPS | Turn Bluetooth off, then retry |

The request format is a non-streamed `POST /chat/completions` with `model`, `messages`, and an optional `Authorization: Bearer …` header. The fixed system message comes first, followed by recent completed turns and the new question. The four documented OpenAI model presets also set `max_completion_tokens: 256` and `reasoning_effort: "none"` to bound generation on this small device. Tokens and characters differ; a reply may be shorter than 500 characters. Other endpoints and custom models retain the compatible request without those model-specific parameters. See [OpenAI's Chat Completions reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create) and [GPT-5.6 Luna model documentation](https://developers.openai.com/api/docs/models/gpt-5.6-luna). The other preset references are [GPT-5.6 Terra](https://developers.openai.com/api/docs/models/gpt-5.6-terra), [GPT-5.6 Sol](https://developers.openai.com/api/docs/models/gpt-5.6-sol) and [GPT-5.4 Mini](https://developers.openai.com/api/docs/models/gpt-5.4-mini). Providers may differ in supported models, access and response formats.

## Validation status

Host tests cover URL/header-injection rejection, the first system message and conversation order, JSON escaping, the 500-character limit, UTF-8 and Unicode surrogate boundaries, byte limits, secret-safe errors, isolated settings persistence, write failure/corruption recovery and malformed response inputs. A live Wi-Fi provider reply passed on the bench. After the conversation UI and 500-character update, the user followed the tap-to-conversation/inline-compose flow and confirmed a real OpenAI reply while Wi-Fi was off; the serial status independently showed Reply received over the LTE test session. The saved API key survived the update and reset. The CST3530 controller was detected at 240×320. Model-picker coverage, longer history scrolling, CST328/v1.0 hardware and negative certificate tests remain separate checks. No API key is bundled with the firmware.


Airplane mode blocks new AI requests. Turning it on interrupts connectivity;
queued requests from before that change cannot resume after radios return.
An interrupted request may have reached the provider and is never automatically
resent. The UI reports that its reply is unknown.
