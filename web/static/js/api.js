(() => {
  const getJson = async (path) => {
    const response = await fetch(path, {
      headers: {
        Accept: "application/json",
      },
    });

    const body = await response.json();
    if (!response.ok) {
      const message = body?.error?.message ?? "request failed";
      throw new Error(message);
    }
    return body;
  };

  window.BlogAlone = Object.freeze({
    getJson,
  });
})();
