# AI chat

Handheld OS can chat with your own **OpenAI-compatible Chat Completions API over Wi-Fi**. The default API base is `https://api.openai.com/v1` and the default model is `gpt-5.6-luna`. You can change both for another compatible provider. This feature needs your provider's API access and may incur its usage charges; a ChatGPT subscription is not an API key.

## Set up on the device

1. Connect Wi-Fi in **Settings → W**.
2. Open **Settings → I** for AI setup.
3. **E** edits the HTTPS API base URL. Keep the default for OpenAI. A full URL ending in `/chat/completions` is also accepted.
4. **M** edits the exact model ID available to your account.
5. **K** enters a replacement API key. Type it on the device, then press Enter to save. The display shows asterisks. Existing keys are never displayed or prefilled. Press Sym to cancel without changing the saved key. Enter on an empty key field removes the key; **X** on AI setup also removes it.
6. Press **A** to open AI chat, then **N** to write. **Enter sends** the question and the recent AI conversation to the configured provider.

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

## Read and continue

- **J/K:** next/previous page.
- **N:** write a follow-up, up to 384 characters.
- **I:** edit AI setup.
- **C:** clear the conversation from this device once the current request finishes.
- **Sym:** return Home. An already submitted request keeps running; Sym does not cancel or undo a request.

The app retains up to four completed question/reply pairs in RAM. Later questions include those recent turns. Replies are limited to 2,048 UTF-8 bytes and the HTTP response to 8 KiB. A shortened reply is identified on screen. Chat is cleared by restarting, clearing the conversation, or saving AI setup. Changing the endpoint also removes the old API key, so it cannot be forwarded to another provider; add the new provider's key afterward. It is separate from SMS/LoRa message history. Clearing locally does not delete data held by your API provider.

UTF-8 text is retained in RAM, while characters outside the built-in e-paper font appear as `?`. This preview supports text responses only. It does not stream, execute model tools, read SMS/contacts, share other device history, or send anything through the cellular modem.

## Connection and credential handling

Only **HTTPS** endpoints are accepted. TLS validates the certificate chain using the pinned ESP32 SDK's bundled public CA roots and checks the hostname. Redirects are refused so a bearer key cannot be forwarded to a different host. Self-signed/local HTTP endpoints are not supported in this preview. When needed, the first explicit send synchronizes the clock using NTP before certificate verification; no API request is sent if clock sync fails.

The key is saved in its own `deckai` NVS namespace. It is **not encrypted at rest**: someone with physical flash access or a full-device backup can recover it. Use a provider key with appropriate spending controls and revoke it if the device or a backup is shared or lost. The key is hidden in the UI, unavailable through USB getters or the BLE companion service, and not written to application logs. Keyboard event tracing is turned off when opening the key-entry field. Keep backups private.

A request runs in a separate worker so the keyboard, SMS modem and UI remain responsive. Only one request runs at a time. Network/TLS timeouts can leave the provider's result uncertain: the firmware **does not automatically retry**. A later, different question includes only completed turns; it does not silently replay a failed prompt. Manually submitting the same question again may create a second billable request.

Common messages:

| Message | Action |
| --- | --- |
| Connect Wi-Fi first | Join a network in Settings → W |
| Add your API key / set endpoint and model | Complete Settings → I |
| Credentials or model access rejected (401/403) | Check the key, provider and permitted model |
| API limit or credit balance reached (429) | Check the provider account's quota or billing |
| Redirect refused | Enter the provider's final HTTPS API address |
| Clock sync / certificate / network timeout | Check Wi-Fi Internet access and the endpoint's certificate; retry manually if appropriate |
| Response exceeded 8 KB / no supported text | Choose a provider/model returning ordinary non-streamed text Chat Completions |
| Not enough memory for HTTPS | Turn Bluetooth off, then retry |

The request format is a non-streamed `POST /chat/completions` with `model`, `messages`, and an optional `Authorization: Bearer …` header. The default OpenAI `gpt-5.6-luna` profile also sets `max_completion_tokens: 1024` and `reasoning_effort: "none"` to bound generation on this small device. Other endpoints/models retain the minimal compatible request. See [OpenAI's Chat Completions reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create) and [GPT-5.6 Luna model documentation](https://developers.openai.com/api/docs/models/gpt-5.6-luna). Providers may differ in supported models, access and response formats.

## Validation status

Host tests cover URL/header-injection rejection, JSON escaping, UTF-8 and Unicode surrogate handling, bounded responses, secret-safe errors, isolated settings persistence, write failure/corruption recovery and malformed response inputs. HTTPS hardware requests, actual provider authentication/billing, and real generated replies require a user-entered key and remain unverified until a provider test is explicitly performed. No API key is bundled with the firmware.
