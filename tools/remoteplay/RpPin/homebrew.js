async function main() {
    const PAYLOAD = window.workingDir + '/RpPin.elf';
    return {
        mainText: "RpPin",
        secondaryText: 'Remote Play PIN for chiaki-ng',
        onclick: async () => {
            return { path: PAYLOAD };
        }
    };
}
