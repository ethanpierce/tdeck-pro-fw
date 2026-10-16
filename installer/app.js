const ESP_WEB_TOOLS = "https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module";
const status = document.querySelector("#load-status");
const connect = document.querySelector("#connect");

async function loadInstaller() {
  const requested = new URLSearchParams(location.search).get("version");
  if (requested && !/^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(requested)) {
    throw new Error("That release version is invalid. Open the installer without ?version to use the latest release.");
  }
  const metadataPath = requested ? `releases/${requested}/release.json` : "release.json";
  const response = await fetch(metadataPath, { cache: "no-store" });
  if (!response.ok) throw new Error("This release is not available. Open the latest release from GitHub.");
  const release = await response.json();
  if (!/^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(release.version)) {
    throw new Error("Release information could not be verified. Please reload this page.");
  }
  document.querySelector("#version").textContent = `${release.version} · test release`;
  document.querySelector("#installer").setAttribute("manifest", `releases/${release.version}/manifest.json`);
  document.querySelector("#secure-link").href = release.installer_url;
  document.querySelector("#release-notes").href = release.release_url;
  for (const name of ["macos", "windows", "source", "checksums"]) {
    const link = document.querySelector(`#download-${name}`);
    link.href = release.downloads[name];
    link.removeAttribute("aria-disabled");
  }
  try {
    await import(ESP_WEB_TOOLS);
  } catch {
    throw new Error("The installer could not load. Check your Internet connection and reload this page.");
  }
  await customElements.whenDefined("esp-web-install-button");
  connect.disabled = false;
  status.textContent = "No app to install. No commands to type.";
}

loadInstaller().catch((error) => {
  document.querySelector("#version").textContent = "Release unavailable";
  status.textContent = error.message || "The installer could not load. Check your Internet connection and reload this page.";
  status.classList.add("notice");
});
