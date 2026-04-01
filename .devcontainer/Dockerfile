ARG DOCKER_TAG=v5.5.1
FROM espressif/idf:${DOCKER_TAG}
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8
RUN apt-get update -y && apt-get install udev curl -y
RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

# Installer Node.js et Claude Code
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - && \
    apt-get install -y nodejs && \
    npm install -g @anthropic-ai/claude-code

ENV PATH="/usr/local/bin:$PATH"

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
CMD ["/bin/bash", "-c"]