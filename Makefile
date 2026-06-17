NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -Iinclude

SRC_DIR = src
OBJ_DIR = build

SRCS = \
	$(SRC_DIR)/core/main.cpp \
	$(SRC_DIR)/core/WebServer.cpp \
	$(SRC_DIR)/core/ClientConnection.cpp \
	$(SRC_DIR)/config/Config.cpp \
	$(SRC_DIR)/config/ConfigTypes.cpp \
	$(SRC_DIR)/http/HttpRequest.cpp \
	$(SRC_DIR)/http/HttpResponse.cpp \
	$(SRC_DIR)/http/HttpHandler.cpp \
	$(SRC_DIR)/cgi/CgiHandler.cpp \
	$(SRC_DIR)/cgi/CgiManager.cpp \
	$(SRC_DIR)/utils/FileUtils.cpp

OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
