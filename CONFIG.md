# cpulimit 配置文件说明

## 概览

cpulimit 现在支持通过配置文件来管理进程排除列表，而不是在代码中硬编码。这使得用户可以根据需要灵活地添加或移除需要排除的进程。

## 配置文件位置

**系统级配置**: `/etc/cpulimit/exclude.conf`

只使用系统级配置文件，确保所有用户的 cpulimit 实例使用相同的排除规则。

## 配置文件格式

```
# 这是注释行，以 # 开头
# 每行一个进程名，不需要引号
# 空行会被忽略
# 行首行尾的空格会被自动去除

# 示例：
bash
top
htop
vim
```

## 管理配置

使用 `install.sh` 脚本可以方便地管理配置：

```bash
# 查看当前配置
./install.sh config show

# 编辑配置文件
./install.sh config edit

# 同步项目配置到系统配置
./install.sh config sync
```

## 配置变更生效

配置文件的变更需要重启 cpulimit 服务才能生效：

```bash
# 重启特定用户的服务
./install.sh restart <username>

# 重启所有服务
./install.sh list  # 先查看所有服务
./install.sh restart user1
./install.sh restart user2
```

## 默认排除的进程类型

- **Shell 进程**: bash, sh, zsh, fish 等
- **SSH 连接**: ssh, sshd
- **终端工具**: tmux, screen
- **编辑器**: vim, vi, nano, emacs
- **系统监控**: top, htop, ps, iostat 等
- **文件工具**: ls, cat, grep, find 等
- **网络工具**: curl, wget, ping 等
- **版本控制**: git, svn
- **容器工具**: docker, kubectl
- **系统工具**: sudo, systemctl

## 自定义排除规则

你可以根据环境需要添加自定义的进程排除规则：

1. 编辑配置文件：`./install.sh config edit`
2. 添加需要排除的进程名（每行一个）
3. 保存并退出
4. 重启相关的 cpulimit 服务

## 注意事项

- 进程名匹配是基于进程的命令名称，而不是完整路径
- 配置变更只影响新启动的 cpulimit 实例
- 建议不要移除系统关键进程（如 bash, ssh 等）的排除规则
- 配置文件需要 root 权限才能修改系统级配置